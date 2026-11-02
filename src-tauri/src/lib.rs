use std::collections::hash_map::RandomState;
use std::hash::{BuildHasher, Hasher};
use std::io::{BufRead, BufReader};
use std::path::PathBuf;
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{mpsc, Arc, Mutex};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use tauri::{Emitter, Manager, RunEvent, WindowEvent};

// Long enough for pydeps to install wheels on a first run.
const STARTUP_TIMEOUT: Duration = Duration::from_secs(120);

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize, PartialEq, Eq)]
pub struct BackendEndpoint {
    pub url: String,
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize, PartialEq, Eq)]
pub struct FileFilter {
    pub name: String,
    #[serde(default)]
    pub extensions: Vec<String>,
}

/// Pure function to parse `ANIMEAN_BACKEND_PORT=<port>` from a single stdout line.
pub fn parse_port_line(line: &str) -> Option<u16> {
    let trimmed = line.trim();
    let rest = trimmed.strip_prefix("ANIMEAN_BACKEND_PORT=")?;
    let port: u16 = rest.trim().parse().ok()?;
    if port == 0 {
        return None;
    }
    Some(port)
}

/// Validates `ANIMEAN_START_PAGE`: only accepts non-empty relative filenames without `..` or `:`.
pub fn parse_start_page(raw: &str) -> Option<String> {
    let trimmed = raw.trim();
    if trimmed.is_empty() {
        return None;
    }
    if trimmed.contains("..") || trimmed.contains(':') {
        return None;
    }
    if trimmed.starts_with('/') || trimmed.starts_with('\\') {
        return None;
    }
    if trimmed.chars().any(|c| c.is_control()) {
        return None;
    }
    let path = std::path::Path::new(trimmed);
    if path.is_absolute() {
        return None;
    }
    let mut has_normal = false;
    for comp in path.components() {
        match comp {
            std::path::Component::Normal(_) => has_normal = true,
            _ => return None,
        }
    }
    if !has_normal {
        return None;
    }
    Some(trimmed.to_string())
}

pub fn format_ws_url(port: u16, token: &str) -> String {
    format!("ws://127.0.0.1:{port}/?token={token}")
}

pub fn generate_token() -> String {
    static COUNTER: AtomicU64 = AtomicU64::new(1);

    fn splitmix64(mut x: u64) -> u64 {
        x = x.wrapping_add(0x9e3779b97f4a7c15);
        x = (x ^ (x >> 30)).wrapping_mul(0xbf58476d1ce4e5b9);
        x = (x ^ (x >> 27)).wrapping_mul(0x94d049bb133111eb);
        x ^ (x >> 31)
    }

    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_nanos() as u64)
        .unwrap_or(0);
    let pid = std::process::id() as u64;
    let seq = COUNTER.fetch_add(1, Ordering::Relaxed);

    let mut h1 = RandomState::new().build_hasher();
    h1.write_u64(nanos);
    h1.write_u64(pid);
    h1.write_u64(seq);
    let high = splitmix64(h1.finish() ^ nanos.rotate_left(17) ^ pid);

    let mut h2 = RandomState::new().build_hasher();
    h2.write_u64(high);
    h2.write_u64(nanos.wrapping_mul(0x517cc1b727220a95));
    h2.write_u64(seq.rotate_left(31));
    let low = splitmix64(h2.finish() ^ nanos ^ pid.rotate_left(32));

    format!("{:016x}{:016x}", high, low)
}

fn resolve_backend_exe() -> Result<PathBuf, String> {
    if let Some(env_val) = std::env::var_os("ANIMEAN_BACKEND_EXE") {
        if !env_val.is_empty() {
            return Ok(PathBuf::from(env_val));
        }
    }
    let current_exe = std::env::current_exe()
        .map_err(|e| format!("Failed to locate shell executable path: {e}"))?;
    let dir = current_exe
        .parent()
        .ok_or_else(|| "Shell executable has no parent directory".to_string())?;
    Ok(dir.join("AnimeAnBackend.exe"))
}

#[cfg(windows)]
struct WinHandle(windows_sys::Win32::Foundation::HANDLE);

#[cfg(windows)]
unsafe impl Send for WinHandle {}
#[cfg(windows)]
unsafe impl Sync for WinHandle {}

#[cfg(windows)]
impl WinHandle {
    fn raw(&self) -> windows_sys::Win32::Foundation::HANDLE {
        self.0
    }
}

#[cfg(windows)]
impl Drop for WinHandle {
    fn drop(&mut self) {
        if !self.0.is_null() {
            unsafe {
                windows_sys::Win32::Foundation::CloseHandle(self.0);
            }
            self.0 = std::ptr::null_mut();
        }
    }
}

#[cfg(windows)]
fn create_kill_on_close_job() -> Result<WinHandle, String> {
    use windows_sys::Win32::Foundation::GetLastError;
    use windows_sys::Win32::System::JobObjects::{
        CreateJobObjectW, JobObjectExtendedLimitInformation, SetInformationJobObject,
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION, JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE,
    };

    let job = unsafe { CreateJobObjectW(std::ptr::null(), std::ptr::null()) };
    if job.is_null() {
        let err = unsafe { GetLastError() };
        return Err(format!("CreateJobObjectW failed (os error {err})"));
    }
    let job_handle = WinHandle(job);

    let mut info: JOBOBJECT_EXTENDED_LIMIT_INFORMATION = unsafe { std::mem::zeroed() };
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

    let ok = unsafe {
        SetInformationJobObject(
            job_handle.0,
            JobObjectExtendedLimitInformation,
            &info as *const _ as *const std::ffi::c_void,
            std::mem::size_of::<JOBOBJECT_EXTENDED_LIMIT_INFORMATION>() as u32,
        )
    };
    if ok == 0 {
        let err = unsafe { GetLastError() };
        return Err(format!("SetInformationJobObject failed (os error {err})"));
    }

    Ok(job_handle)
}

#[cfg(windows)]
fn assign_child_to_job(job: &WinHandle, child: &Child) -> Result<(), String> {
    use std::os::windows::io::AsRawHandle;
    use windows_sys::Win32::Foundation::{GetLastError, HANDLE};
    use windows_sys::Win32::System::JobObjects::AssignProcessToJobObject;

    let proc_handle = child.as_raw_handle() as HANDLE;
    let ok = unsafe { AssignProcessToJobObject(job.0, proc_handle) };
    if ok == 0 {
        let err = unsafe { GetLastError() };
        return Err(format!("AssignProcessToJobObject failed (os error {err})"));
    }
    Ok(())
}

#[cfg(windows)]
fn duplicate_child_handle(child: &Child) -> Result<WinHandle, String> {
    use std::os::windows::io::AsRawHandle;
    use windows_sys::Win32::Foundation::{
        DuplicateHandle, GetLastError, DUPLICATE_SAME_ACCESS, HANDLE,
    };
    use windows_sys::Win32::System::Threading::GetCurrentProcess;

    let mut dup: HANDLE = std::ptr::null_mut();
    let current = unsafe { GetCurrentProcess() };
    let src = child.as_raw_handle() as HANDLE;
    let ok = unsafe {
        DuplicateHandle(
            current,
            src,
            current,
            &mut dup,
            0,
            0,
            DUPLICATE_SAME_ACCESS,
        )
    };
    if ok == 0 || dup.is_null() {
        let err = unsafe { GetLastError() };
        return Err(format!("DuplicateHandle failed (os error {err})"));
    }
    Ok(WinHandle(dup))
}

struct BackendInner {
    url: Option<String>,
    error: Option<String>,
    child: Option<Child>,
    #[cfg(windows)]
    job: Option<WinHandle>,
}

pub struct BackendManager {
    inner: Mutex<BackendInner>,
    shutting_down: AtomicBool,
}

impl BackendManager {
    fn new_failed(err: String) -> Self {
        Self {
            inner: Mutex::new(BackendInner {
                url: None,
                error: Some(err),
                child: None,
                #[cfg(windows)]
                job: None,
            }),
            shutting_down: AtomicBool::new(false),
        }
    }

    fn spawn_and_wait() -> (Arc<Self>, Option<WinHandle>) {
        let exe_path = match resolve_backend_exe() {
            Ok(p) => p,
            Err(e) => return (Arc::new(Self::new_failed(e)), None),
        };

        #[cfg(windows)]
        let job = match create_kill_on_close_job() {
            Ok(j) => j,
            Err(e) => return (Arc::new(Self::new_failed(e)), None),
        };

        let token = generate_token();
        let mut cmd = Command::new(&exe_path);
        cmd.arg("--port")
            .arg("0")
            .arg("--token")
            .arg(&token)
            .stdin(Stdio::null())
            .stdout(Stdio::piped())
            .stderr(Stdio::null());

        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            use windows_sys::Win32::System::Threading::CREATE_NO_WINDOW;
            cmd.creation_flags(CREATE_NO_WINDOW);
        }

        let mut child = match cmd.spawn() {
            Ok(c) => c,
            Err(e) => {
                return (
                    Arc::new(Self::new_failed(format!(
                        "Failed to start backend ({}): {e}",
                        exe_path.display()
                    ))),
                    None,
                );
            }
        };

        #[cfg(windows)]
        if let Err(e) = assign_child_to_job(&job, &child) {
            let _ = child.kill();
            let _ = child.wait();
            return (Arc::new(Self::new_failed(e)), None);
        }

        let stdout = match child.stdout.take() {
            Some(s) => s,
            None => {
                let _ = child.kill();
                let _ = child.wait();
                return (
                    Arc::new(Self::new_failed(
                        "Failed to capture backend stdout".to_string(),
                    )),
                    None,
                );
            }
        };

        let (tx, rx) = mpsc::channel::<Result<u16, String>>();
        std::thread::spawn(move || {
            let mut reader = BufReader::new(stdout);
            let mut line = String::new();
            let mut reported = false;
            loop {
                line.clear();
                match reader.read_line(&mut line) {
                    Ok(0) => {
                        if !reported {
                            let _ = tx.send(Err(
                                "Backend exited before reporting ANIMEAN_BACKEND_PORT".to_string(),
                            ));
                        }
                        break;
                    }
                    Ok(_) => {
                        if !reported {
                            if let Some(port) = parse_port_line(&line) {
                                let _ = tx.send(Ok(port));
                                reported = true;
                            }
                        }
                    }
                    Err(e) => {
                        if !reported {
                            let _ = tx.send(Err(format!("Error reading backend stdout: {e}")));
                        }
                        break;
                    }
                }
            }
        });

        let port = match rx.recv_timeout(STARTUP_TIMEOUT) {
            Ok(Ok(port)) => port,
            Ok(Err(e)) => {
                let _ = child.kill();
                let _ = child.wait();
                return (Arc::new(Self::new_failed(e)), None);
            }
            Err(mpsc::RecvTimeoutError::Timeout) => {
                let _ = child.kill();
                let _ = child.wait();
                return (
                    Arc::new(Self::new_failed(
                        "Timed out waiting for ANIMEAN_BACKEND_PORT from backend".to_string(),
                    )),
                    None,
                );
            }
            Err(mpsc::RecvTimeoutError::Disconnected) => {
                let _ = child.kill();
                let _ = child.wait();
                return (
                    Arc::new(Self::new_failed(
                        "Backend stdout reader disconnected before reporting port".to_string(),
                    )),
                    None,
                );
            }
        };

        #[cfg(windows)]
        let wait_handle = duplicate_child_handle(&child).ok();

        let url = format_ws_url(port, &token);
        let manager = Arc::new(Self {
            inner: Mutex::new(BackendInner {
                url: Some(url),
                error: None,
                child: Some(child),
                #[cfg(windows)]
                job: Some(job),
            }),
            shutting_down: AtomicBool::new(false),
        });

        (manager, wait_handle)
    }

    fn endpoint(&self) -> Result<BackendEndpoint, String> {
        let mut guard = self
            .inner
            .lock()
            .map_err(|_| "Backend state lock poisoned".to_string())?;

        if let Some(child) = guard.child.as_mut() {
            match child.try_wait() {
                Ok(Some(status)) => {
                    let msg = format!("Backend process has exited ({status})");
                    guard.url = None;
                    guard.error = Some(msg.clone());
                    guard.child = None;
                    return Err(msg);
                }
                Ok(None) => {}
                Err(e) => {
                    let msg = format!("Failed to query backend process status: {e}");
                    guard.url = None;
                    guard.error = Some(msg.clone());
                    guard.child = None;
                    return Err(msg);
                }
            }
        }

        if let Some(url) = guard.url.as_ref() {
            Ok(BackendEndpoint { url: url.clone() })
        } else {
            Err(guard
                .error
                .clone()
                .unwrap_or_else(|| "Backend is not running".to_string()))
        }
    }

    fn mark_unexpected_exit(&self, code: Option<u32>) {
        if let Ok(mut guard) = self.inner.lock() {
            if let Some(mut child) = guard.child.take() {
                let _ = child.wait();
            }
            guard.url = None;
            guard.error = Some(match code {
                Some(c) => format!("Backend exited unexpectedly (exit code {c})"),
                None => "Backend exited unexpectedly".to_string(),
            });
        }
    }

    fn shutdown(&self) {
        self.shutting_down.store(true, Ordering::SeqCst);
        if let Ok(mut guard) = self.inner.lock() {
            if let Some(mut child) = guard.child.take() {
                let _ = child.kill();
                let _ = child.wait();
            }
            #[cfg(windows)]
            {
                guard.job.take();
            }
            guard.url = None;
            guard.error = Some("Backend shut down".to_string());
        }
    }
}

impl Drop for BackendManager {
    fn drop(&mut self) {
        self.shutdown();
    }
}

#[cfg(windows)]
fn spawn_exit_watcher(
    app_handle: tauri::AppHandle,
    manager: Arc<BackendManager>,
    wait_handle: WinHandle,
) {
    use windows_sys::Win32::System::Threading::{
        GetExitCodeProcess, WaitForSingleObject, INFINITE,
    };

    std::thread::spawn(move || {
        unsafe {
            WaitForSingleObject(wait_handle.raw(), INFINITE);
        }
        if manager.shutting_down.load(Ordering::SeqCst) {
            return;
        }
        let mut exit_code: u32 = 0;
        let has_code = unsafe { GetExitCodeProcess(wait_handle.raw(), &mut exit_code) } != 0;
        let code_opt = if has_code { Some(exit_code) } else { None };
        manager.mark_unexpected_exit(code_opt);
        let _ = app_handle.emit_to("main", "backend-exited", code_opt);
    });
}

fn validate_panel_label(label: &str) -> Result<&str, String> {
    let trimmed = label.trim();
    if trimmed.is_empty() {
        return Err("Panel window label cannot be empty".to_string());
    }
    if trimmed == "main" {
        return Err("Cannot use reserved label 'main' for panel window".to_string());
    }
    if !trimmed.starts_with("panel-") || trimmed.len() <= "panel-".len() {
        return Err(format!(
            "Invalid panel window label '{trimmed}': must start with 'panel-'"
        ));
    }
    if !trimmed
        .chars()
        .all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_')
    {
        return Err(format!("Invalid characters in panel window label '{trimmed}'"));
    }
    Ok(trimmed)
}

fn min_size_for_panel(
    label: &str,
    min_width: Option<f64>,
    min_height: Option<f64>,
) -> (f64, f64) {
    let default_min_w = if label == "panel-timelineDock" {
        134.0
    } else {
        80.0
    };
    let default_min_h = 60.0;
    let w = min_width.unwrap_or(default_min_w).max(default_min_w);
    let h = min_height.unwrap_or(default_min_h).max(default_min_h);
    (w, h)
}

fn parse_webview_url(url: &str) -> Result<tauri::WebviewUrl, String> {
    let trimmed = url.trim();
    if trimmed.starts_with("http://")
        || trimmed.starts_with("https://")
        || trimmed.starts_with("tauri://")
    {
        let parsed: tauri::Url = trimmed
            .parse()
            .map_err(|e| format!("Invalid panel URL '{trimmed}': {e}"))?;
        // Panels are our own pages: the dev server or the bundled app.
        match parsed.host_str() {
            Some("localhost") | Some("127.0.0.1") | Some("tauri.localhost") => {
                Ok(tauri::WebviewUrl::External(parsed))
            }
            other => Err(format!("Refusing panel URL on host {other:?}")),
        }
    } else {
        let rel = trimmed.trim_start_matches('/');
        let path_part = rel.split('?').next().unwrap_or(rel);
        if path_part.contains("..")
            || path_part.contains('\\')
            || path_part.contains(':')
            || trimmed.contains("://")
        {
            return Err(format!("Invalid relative panel URL '{trimmed}'"));
        }
        let target = if rel.is_empty() { "index.html" } else { rel };
        Ok(tauri::WebviewUrl::App(PathBuf::from(target)))
    }
}

#[tauri::command]
fn backend_endpoint(
    state: tauri::State<'_, Arc<BackendManager>>,
) -> Result<BackendEndpoint, String> {
    state.endpoint()
}

#[tauri::command]
fn set_window_title(
    app: tauri::AppHandle,
    window: tauri::WebviewWindow,
    title: String,
) -> Result<(), String> {
    if let Some(main_win) = app.get_webview_window("main") {
        main_win.set_title(&title).map_err(|e| e.to_string())
    } else {
        window.set_title(&title).map_err(|e| e.to_string())
    }
}

#[tauri::command]
async fn open_panel_window(
    app: tauri::AppHandle,
    label: String,
    url: String,
    x: f64,
    y: f64,
    width: f64,
    height: f64,
    min_width: Option<f64>,
    min_height: Option<f64>,
) -> Result<(), String> {
    let valid_label = validate_panel_label(&label)?.to_string();
    let main_win = app
        .get_webview_window("main")
        .ok_or_else(|| "Main window 'main' not found".to_string())?;
    let (min_w, min_h) = min_size_for_panel(&valid_label, min_width, min_height);
    let w = width.max(min_w);
    let h = height.max(min_h);

    if let Some(existing) = app.get_webview_window(&valid_label) {
        let _ = existing.set_min_size(Some(tauri::Size::Logical(tauri::LogicalSize::new(
            min_w, min_h,
        ))));
        let _ =
            existing.set_position(tauri::Position::Logical(tauri::LogicalPosition::new(x, y)));
        let _ = existing.set_size(tauri::Size::Logical(tauri::LogicalSize::new(w, h)));
        let _ = existing.show();
        let _ = existing.set_focus();
        return Ok(());
    }

    let webview_url = parse_webview_url(&url)?;
    tauri::WebviewWindowBuilder::new(&app, &valid_label, webview_url)
        .title("AnimeAn")
        .parent(&main_win)
        .map_err(|e| e.to_string())?
        .skip_taskbar(true)
        .decorations(false)
        .resizable(true)
        .position(x, y)
        .inner_size(w, h)
        .min_inner_size(min_w, min_h)
        .build()
        .map(|_| ())
        .map_err(|e| e.to_string())
}

#[tauri::command]
async fn close_panel_window(app: tauri::AppHandle, label: String) -> Result<(), String> {
    let valid_label = validate_panel_label(&label)?;
    if let Some(win) = app.get_webview_window(valid_label) {
        win.close().map_err(|e| e.to_string())?;
    }
    Ok(())
}

fn file_path_to_string(fp: tauri_plugin_dialog::FilePath) -> String {
    match fp {
        tauri_plugin_dialog::FilePath::Path(p) => p.to_string_lossy().into_owned(),
        tauri_plugin_dialog::FilePath::Url(u) => u
            .to_file_path()
            .map(|p| p.to_string_lossy().into_owned())
            .unwrap_or_else(|_| u.to_string()),
    }
}

#[tauri::command]
async fn pick_files(
    app: tauri::AppHandle,
    window: tauri::WebviewWindow,
    mode: String,
    title: Option<String>,
    filters: Option<Vec<FileFilter>>,
    multiple: Option<bool>,
    default_name: Option<String>,
) -> Result<Vec<String>, String> {
    use tauri_plugin_dialog::DialogExt;

    let normalized_mode = mode.trim().to_ascii_lowercase();
    if normalized_mode != "open" && normalized_mode != "save" {
        return Err(format!(
            "Unsupported pick_files mode '{mode}': expected 'open' or 'save'"
        ));
    }

    let mut builder = app.dialog().file().set_parent(&window);
    if let Some(t) = title {
        if !t.is_empty() {
            builder = builder.set_title(t);
        }
    }
    if let Some(name) = default_name {
        if !name.is_empty() {
            builder = builder.set_file_name(name);
        }
    }
    for filter in filters.unwrap_or_default() {
        let exts: Vec<&str> = filter
            .extensions
            .iter()
            .map(|ext| ext.trim_start_matches('.'))
            .filter(|ext| !ext.is_empty())
            .collect();
        if !exts.is_empty() {
            builder = builder.add_filter(&filter.name, &exts);
        }
    }

    let is_multiple = multiple.unwrap_or(false);
    tauri::async_runtime::spawn_blocking(move || {
        if normalized_mode == "save" {
            Ok(builder
                .blocking_save_file()
                .map(|p| vec![file_path_to_string(p)])
                .unwrap_or_default())
        } else if is_multiple {
            Ok(builder
                .blocking_pick_files()
                .map(|list| list.into_iter().map(file_path_to_string).collect())
                .unwrap_or_default())
        } else {
            Ok(builder
                .blocking_pick_file()
                .map(|p| vec![file_path_to_string(p)])
                .unwrap_or_default())
        }
    })
    .await
    .map_err(|e| format!("File dialog task failed: {e}"))?
}

pub fn run() {
    let start_page = std::env::var("ANIMEAN_START_PAGE")
        .ok()
        .and_then(|val| parse_start_page(&val));

    let (backend_manager, wait_handle) = if let Some(ref page) = start_page {
        (
            Arc::new(BackendManager::new_failed(format!(
                "Backend not started (ANIMEAN_START_PAGE={page})"
            ))),
            None,
        )
    } else {
        BackendManager::spawn_and_wait()
    };
    let manager_for_setup = Arc::clone(&backend_manager);
    let manager_for_window = Arc::clone(&backend_manager);
    let manager_for_run = Arc::clone(&backend_manager);

    let mut context = tauri::generate_context!();
    if let Some(ref page) = start_page {
        for win in &mut context.config_mut().app.windows {
            if win.label == "main" {
                win.url = tauri::WebviewUrl::App(PathBuf::from(page));
            }
        }
    }

    let app = tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .manage(backend_manager)
        .setup(move |app| {
            #[cfg(windows)]
            if let Some(wh) = wait_handle {
                spawn_exit_watcher(app.handle().clone(), manager_for_setup, wh);
            }
            Ok(())
        })
        .on_window_event(move |window, event| {
            if window.label() == "main" {
                if matches!(
                    event,
                    WindowEvent::CloseRequested { .. } | WindowEvent::Destroyed
                ) {
                    for (lbl, win) in window.app_handle().webview_windows() {
                        if lbl.starts_with("panel-") {
                            let _ = win.close();
                        }
                    }
                }
                if let WindowEvent::Destroyed = event {
                    manager_for_window.shutdown();
                    window.app_handle().exit(0);
                }
            }
        })
        .invoke_handler(tauri::generate_handler![
            backend_endpoint,
            set_window_title,
            open_panel_window,
            close_panel_window,
            pick_files,
        ])
        .build(context)
        .expect("error while building AnimeAn Tauri shell");

    app.run(move |_app_handle, event| {
        if let RunEvent::Exit | RunEvent::ExitRequested { .. } = event {
            manager_for_run.shutdown();
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_port_line_valid() {
        assert_eq!(parse_port_line("ANIMEAN_BACKEND_PORT=12345"), Some(12345));
        assert_eq!(
            parse_port_line("ANIMEAN_BACKEND_PORT=54321\r\n"),
            Some(54321)
        );
        assert_eq!(
            parse_port_line("  ANIMEAN_BACKEND_PORT=8080 \n"),
            Some(8080)
        );
        assert_eq!(parse_port_line("ANIMEAN_BACKEND_PORT=1"), Some(1));
        assert_eq!(parse_port_line("ANIMEAN_BACKEND_PORT=65535"), Some(65535));
    }

    #[test]
    fn test_parse_port_line_invalid() {
        assert_eq!(parse_port_line(""), None);
        assert_eq!(parse_port_line("   "), None);
        assert_eq!(parse_port_line("ANIMEAN_BACKEND_PORT="), None);
        assert_eq!(parse_port_line("ANIMEAN_BACKEND_PORT=0"), None);
        assert_eq!(parse_port_line("ANIMEAN_BACKEND_PORT=65536"), None);
        assert_eq!(parse_port_line("ANIMEAN_BACKEND_PORT=-10"), None);
        assert_eq!(parse_port_line("ANIMEAN_BACKEND_PORT=abc"), None);
        assert_eq!(parse_port_line("ANIMEAN_BACKEND_PORT=1234a"), None);
        assert_eq!(parse_port_line("OTHER_PORT=12345"), None);
        assert_eq!(parse_port_line("INFO ANIMEAN_BACKEND_PORT=12345"), None);
    }

    #[test]
    fn test_parse_start_page() {
        assert_eq!(
            parse_start_page("pen_probe.html"),
            Some("pen_probe.html".to_string())
        );
        assert_eq!(
            parse_start_page("  pen_probe.html \r\n"),
            Some("pen_probe.html".to_string())
        );
        assert_eq!(
            parse_start_page("tools/pen_probe.html"),
            Some("tools/pen_probe.html".to_string())
        );

        assert_eq!(parse_start_page(""), None);
        assert_eq!(parse_start_page("   "), None);
        assert_eq!(parse_start_page("."), None);
        assert_eq!(parse_start_page(".."), None);
        assert_eq!(parse_start_page("../pen_probe.html"), None);
        assert_eq!(parse_start_page("a/../pen_probe.html"), None);
        assert_eq!(parse_start_page("foo..bar.html"), None);
        assert_eq!(parse_start_page("http://localhost:1430/pen_probe.html"), None);
        assert_eq!(parse_start_page("tauri://localhost/pen_probe.html"), None);
        assert_eq!(parse_start_page("C:\\pen_probe.html"), None);
        assert_eq!(parse_start_page("C:/pen_probe.html"), None);
        assert_eq!(parse_start_page("/pen_probe.html"), None);
        assert_eq!(parse_start_page("\\pen_probe.html"), None);
    }

    #[test]
    fn test_generate_token_format_and_uniqueness() {
        let t1 = generate_token();
        let t2 = generate_token();
        assert_eq!(t1.len(), 32);
        assert_eq!(t2.len(), 32);
        assert!(t1.chars().all(|c| matches!(c, '0'..='9' | 'a'..='f')));
        assert!(t2.chars().all(|c| matches!(c, '0'..='9' | 'a'..='f')));
        assert_ne!(t1, t2);
    }

    #[test]
    fn test_format_ws_url() {
        let url = format_ws_url(9876, "0123456789abcdef0123456789abcdef");
        assert_eq!(
            url,
            "ws://127.0.0.1:9876/?token=0123456789abcdef0123456789abcdef"
        );
    }

    #[test]
    fn test_file_filter_deserialize_and_path_conversion() {
        let raw = r#"[{"name":"Images","extensions":["png","jpg"]},{"name":"All"}]"#;
        let filters: Vec<FileFilter> = serde_json::from_str(raw).unwrap();
        assert_eq!(filters.len(), 2);
        assert_eq!(filters[0].name, "Images");
        assert_eq!(filters[0].extensions, vec!["png", "jpg"]);
        assert_eq!(filters[1].name, "All");
        assert!(filters[1].extensions.is_empty());

        let fp = tauri_plugin_dialog::FilePath::Path(PathBuf::from(r"C:\test\sample.png"));
        assert_eq!(file_path_to_string(fp), r"C:\test\sample.png");
    }

    #[test]
    fn test_parse_webview_url() {
        match parse_webview_url("index.html?only=toolsDock&satellite=1").unwrap() {
            tauri::WebviewUrl::App(p) => {
                assert_eq!(p, PathBuf::from("index.html?only=toolsDock&satellite=1"));
            }
            other => panic!("Expected WebviewUrl::App, got {other:?}"),
        }
        match parse_webview_url("/index.html?only=timelineDock&satellite=1").unwrap() {
            tauri::WebviewUrl::App(p) => {
                assert_eq!(p, PathBuf::from("index.html?only=timelineDock&satellite=1"));
            }
            other => panic!("Expected WebviewUrl::App, got {other:?}"),
        }
        match parse_webview_url("").unwrap() {
            tauri::WebviewUrl::App(p) => assert_eq!(p, PathBuf::from("index.html")),
            other => panic!("Expected WebviewUrl::App, got {other:?}"),
        }
        assert!(matches!(
            parse_webview_url("http://localhost:1430/index.html?only=layersDock&satellite=1")
                .unwrap(),
            tauri::WebviewUrl::External(_)
        ));
        assert!(matches!(
            parse_webview_url("http://127.0.0.1:1430/index.html").unwrap(),
            tauri::WebviewUrl::External(_)
        ));
        assert!(matches!(
            parse_webview_url("tauri://localhost/index.html").unwrap(),
            tauri::WebviewUrl::External(_)
        ));
        assert!(parse_webview_url("https://example.com/index.html").is_err());
        assert!(parse_webview_url("../secret.html").is_err());
        assert!(parse_webview_url("javascript:alert(1)").is_err());
        assert!(parse_webview_url("file:///C:/Windows/win.ini").is_err());
    }

    #[test]
    fn test_validate_panel_label_and_min_size() {
        assert_eq!(
            validate_panel_label("panel-toolsDock").unwrap(),
            "panel-toolsDock"
        );
        assert_eq!(
            validate_panel_label("panel-timelineDock").unwrap(),
            "panel-timelineDock"
        );
        assert_eq!(
            validate_panel_label("  panel-layers_1 ").unwrap(),
            "panel-layers_1"
        );
        assert!(validate_panel_label("").is_err());
        assert!(validate_panel_label("main").is_err());
        assert!(validate_panel_label("panel-").is_err());
        assert!(validate_panel_label("toolsDock").is_err());
        assert!(validate_panel_label("panel-tools/dock").is_err());

        assert_eq!(
            min_size_for_panel("panel-toolsDock", None, None),
            (80.0, 60.0)
        );
        assert_eq!(
            min_size_for_panel("panel-timelineDock", None, None),
            (134.0, 60.0)
        );
        assert_eq!(
            min_size_for_panel("panel-layersDock", Some(120.0), Some(90.0)),
            (120.0, 90.0)
        );
        assert_eq!(
            min_size_for_panel("panel-layersDock", Some(20.0), Some(10.0)),
            (80.0, 60.0)
        );
    }
}
