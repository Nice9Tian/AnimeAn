use std::collections::hash_map::RandomState;
use std::collections::HashMap;
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

/// Dialog windows are "dialog-<id>", the id pyfile/dialogs.py hands out.
pub fn validate_dialog_label(label: &str) -> Result<&str, String> {
    let trimmed = label.trim();
    let id = trimmed
        .strip_prefix("dialog-")
        .ok_or_else(|| format!("Invalid dialog window label '{trimmed}': must start with 'dialog-'"))?;
    if id.is_empty() || id.len() > 32 || !id.chars().all(|c| c.is_ascii_alphanumeric() || c == '_') {
        return Err(format!("Invalid dialog window label '{trimmed}'"));
    }
    Ok(trimmed)
}

/// A window a dialog may be owned by, or a lock may name: the main window or
/// another dialog.
pub fn validate_owner_label(label: &str) -> Result<&str, String> {
    if label.trim() == "main" {
        Ok("main")
    } else {
        validate_dialog_label(label)
    }
}

/// Where a dialog of `size` sits over the middle of its owner, in logical
/// pixels, never above the owner's top edge.
pub fn center_over(owner_pos: (f64, f64), owner_size: (f64, f64), size: (f64, f64)) -> (f64, f64) {
    let x = owner_pos.0 + (owner_size.0 - size.0) / 2.0;
    let y = owner_pos.1 + ((owner_size.1 - size.1) / 2.0).max(0.0);
    (x.round(), y.round())
}

/// Where a dialog of `size` goes: over the middle of its owner, kept inside
/// `area`, the work area of the owner's monitor. Frames are (top-left, size) in
/// logical pixels. A minimized owner (`None`) has no frame on screen (Windows
/// parks it near -32000), so the dialog goes to the middle of `area` and waits
/// there for the owner to come back.
pub fn place_dialog(
    owner: Option<((f64, f64), (f64, f64))>,
    area: Option<((f64, f64), (f64, f64))>,
    size: (f64, f64),
) -> (f64, f64) {
    let Some((frame_pos, frame_size)) = owner.or(area) else {
        return (0.0, 0.0);
    };
    let (mut x, mut y) = center_over(frame_pos, frame_size, size);
    if let Some(((ax, ay), (aw, ah))) = area {
        // Left and top last: a dialog bigger than the area keeps its title bar on it.
        x = x.min(ax + aw - size.0).max(ax);
        y = y.min(ay + ah - size.1).max(ay);
    }
    (x.round(), y.round())
}

fn logical_frame(win: &tauri::WebviewWindow) -> Result<((f64, f64), (f64, f64)), String> {
    let scale = win.scale_factor().map_err(|e| e.to_string())?;
    let pos = win.outer_position().map_err(|e| e.to_string())?.to_logical::<f64>(scale);
    let size = win.outer_size().map_err(|e| e.to_string())?.to_logical::<f64>(scale);
    Ok(((pos.x, pos.y), (size.width, size.height)))
}

fn dialog_position(owner_win: &tauri::WebviewWindow, size: (f64, f64)) -> Result<(f64, f64), String> {
    let scale = owner_win.scale_factor().map_err(|e| e.to_string())?;
    // A minimized window's monitor is the one it was on before it was minimized.
    let monitor = owner_win
        .current_monitor()
        .ok()
        .flatten()
        .or_else(|| owner_win.primary_monitor().ok().flatten());
    let area = monitor.map(|monitor| {
        let rect = monitor.work_area();
        let pos = rect.position.to_logical::<f64>(scale);
        let size = rect.size.to_logical::<f64>(scale);
        ((pos.x, pos.y), (size.width, size.height))
    });
    let owner = if owner_win.is_minimized().unwrap_or(false) {
        None
    } else {
        Some(logical_frame(owner_win)?)
    };
    Ok(place_dialog(owner, area, size))
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

/// Where a panel window was when it closed, in the units `open_panel_window` takes.
#[derive(Debug, Clone, Copy, serde::Serialize, PartialEq)]
struct PanelRect {
    x: f64,
    y: f64,
    width: f64,
    height: f64,
}

#[tauri::command]
async fn close_panel_window(
    app: tauri::AppHandle,
    label: String,
) -> Result<Option<PanelRect>, String> {
    let valid_label = validate_panel_label(&label)?;
    let Some(win) = app.get_webview_window(valid_label) else {
        return Ok(None);
    };
    let scale = win.scale_factor().map_err(|e| e.to_string())?;
    let position = win
        .outer_position()
        .map_err(|e| e.to_string())?
        .to_logical::<f64>(scale);
    let size = win
        .inner_size()
        .map_err(|e| e.to_string())?
        .to_logical::<f64>(scale);
    win.close().map_err(|e| e.to_string())?;
    Ok(Some(PanelRect {
        x: position.x,
        y: position.y,
        width: size.width,
        height: size.height,
    }))
}

// ---- dialog windows -----------------------------------------------------------
// Which dialog opens and whether it is modal is Python's (pyfile/dialogs.py);
// these only make windows. A dialog window has no frame of its own, stays out of
// the taskbar and is owned by the window under it, so it stays above that window
// and goes behind other programs with it.

#[tauri::command]
async fn open_dialog_window(
    app: tauri::AppHandle,
    label: String,
    url: String,
    owner: String,
    title: String,
) -> Result<(), String> {
    let label = validate_dialog_label(&label)?.to_string();
    let owner_label = validate_owner_label(&owner)?;
    let owner_win = app
        .get_webview_window(owner_label)
        .ok_or_else(|| format!("Owner window '{owner_label}' not found"))?;
    if app.get_webview_window(&label).is_some() {
        return Ok(());
    }
    let size = (320.0, 200.0);
    let (x, y) = dialog_position(&owner_win, size)?;
    tauri::WebviewWindowBuilder::new(&app, &label, parse_webview_url(&url)?)
        .title(title)
        .parent(&owner_win)
        .map_err(|e| e.to_string())?
        .skip_taskbar(true)
        .decorations(false)
        .resizable(false)
        .visible(false)
        .position(x, y)
        .inner_size(size.0, size.1)
        .build()
        .map(|_| ())
        .map_err(|e| e.to_string())
}

/// The page has drawn its dialog: size the window to it, centre it over its
/// owner, show it and give it the input.
#[tauri::command]
async fn show_dialog_window(
    app: tauri::AppHandle,
    label: String,
    owner: String,
    width: f64,
    height: f64,
) -> Result<(), String> {
    let label = validate_dialog_label(&label)?;
    let win = app
        .get_webview_window(label)
        .ok_or_else(|| format!("Dialog window '{label}' not found"))?;
    let size = (width.clamp(80.0, 4000.0), height.clamp(40.0, 4000.0));
    win.set_size(tauri::Size::Logical(tauri::LogicalSize::new(size.0, size.1)))
        .map_err(|e| e.to_string())?;
    if let Some(owner_win) = app.get_webview_window(validate_owner_label(&owner)?) {
        let (x, y) = dialog_position(&owner_win, size)?;
        let _ = win.set_position(tauri::Position::Logical(tauri::LogicalPosition::new(x, y)));
    }
    win.show().map_err(|e| e.to_string())?;
    win.set_focus().map_err(|e| e.to_string())
}

#[tauri::command]
async fn close_dialog_window(app: tauri::AppHandle, label: String) -> Result<(), String> {
    let label = validate_dialog_label(&label)?;
    match app.get_webview_window(label) {
        // destroy, not close: a close request is the user's answer (see run()).
        Some(win) => win.destroy().map_err(|e| e.to_string()),
        None => Ok(()),
    }
}

/// Disabling a dialog's owner is Windows' own modality: the owner takes no
/// input, and a press on it brings the dialog forward.
#[tauri::command]
fn set_window_enabled(app: tauri::AppHandle, label: String, enabled: bool) -> Result<(), String> {
    let label = validate_owner_label(&label)?;
    let win = app
        .get_webview_window(label)
        .ok_or_else(|| format!("Window '{label}' not found"))?;
    win.set_enabled(enabled).map_err(|e| e.to_string())
}

/// A panel window that cannot be activated does not rise above the dialog when
/// it is clicked; its page still hears the press and asks for focus_modal.
/// Every panel window, whichever page opened it.
#[tauri::command]
fn set_panels_focusable(app: tauri::AppHandle, focusable: bool) -> Result<(), String> {
    for (label, win) in app.webview_windows() {
        if label.starts_with("panel-") {
            win.set_focusable(focusable).map_err(|e| e.to_string())?;
        }
    }
    Ok(())
}

/// One top-level window of this process, for `modal_target`.
#[derive(Debug, Clone, Copy)]
pub struct TopWindow {
    pub id: isize,
    /// The end of its owner chain (itself when it has no owner).
    pub root_owner: isize,
    pub shown: bool,
    pub enabled: bool,
    pub panel: bool,
}

/// What holds the input for `main` while a modal dialog or a native picker is
/// up: the topmost shown, enabled window owned by `main` at any depth, panel
/// windows aside. `windows` runs topmost first. The owner of a modal one is
/// disabled, so a dialog under another or under a picker is never picked.
pub fn modal_target(main: isize, windows: &[TopWindow]) -> Option<isize> {
    windows
        .iter()
        .find(|w| w.id != main && !w.panel && w.shown && w.enabled && w.root_owner == main)
        .map(|w| w.id)
}

/// Gives the input to whatever holds it for the main window: the dialog on top,
/// or a native picker. Not simply the main window's last active popup: a real
/// click on a panel window lets WebView2 focus it although it cannot be
/// activated, the panel becomes that popup and stays above the dialog.
#[tauri::command]
fn focus_modal(app: tauri::AppHandle) -> Result<(), String> {
    let main_win = app
        .get_webview_window("main")
        .ok_or_else(|| "Main window 'main' not found".to_string())?;
    #[cfg(windows)]
    {
        use windows_sys::Win32::Foundation::HWND;
        use windows_sys::Win32::UI::WindowsAndMessaging::{
            GetAncestor, GetLastActivePopup, GetWindowLongPtrW, GetWindowThreadProcessId, IsWindowVisible,
            SetForegroundWindow, GA_ROOTOWNER, GWL_STYLE, WS_DISABLED,
        };
        let main = main_win.hwnd().map_err(|e| e.to_string())?.0 as HWND;
        let panels: std::collections::HashSet<isize> = app
            .webview_windows()
            .into_iter()
            .filter(|(label, _)| label.starts_with("panel-"))
            .filter_map(|(_, win)| win.hwnd().ok().map(|h| h.0 as isize))
            .collect();
        let pid = std::process::id();
        let windows: Vec<TopWindow> = windows_in_z_order()
            .into_iter()
            .filter(|&h| {
                let mut owner_pid = 0u32;
                unsafe { GetWindowThreadProcessId(h, &mut owner_pid) };
                owner_pid == pid
            })
            .map(|h| unsafe {
                TopWindow {
                    id: h as isize,
                    root_owner: GetAncestor(h, GA_ROOTOWNER) as isize,
                    shown: IsWindowVisible(h) != 0,
                    enabled: (GetWindowLongPtrW(h, GWL_STYLE) as u32 & WS_DISABLED) == 0,
                    panel: panels.contains(&(h as isize)),
                }
            })
            .collect();
        let target = modal_target(main as isize, &windows).or_else(|| {
            // Nothing shown yet (a dialog page still loading): what Windows would pick.
            let popup = unsafe { GetLastActivePopup(main) } as isize;
            (popup != 0 && popup != main as isize && !panels.contains(&popup)).then_some(popup)
        });
        if let Some(hwnd) = target {
            unsafe { SetForegroundWindow(hwnd as HWND) };
        }
        Ok(())
    }
    #[cfg(not(windows))]
    {
        main_win.set_focus().map_err(|e| e.to_string())
    }
}

// ---- menus over other windows --------------------------------------------------
// A menu is drawn in the page that opened it. While it shows, the AnimeAn windows
// above that page's window (the floating panels over the main window) leave the
// menu's rectangles open, so the menu is seen and takes the clicks.

/// The parts of `menus` (screen rectangles: left, top, right, bottom) that fall on
/// a window at `window`, in that window's own coordinates.
pub fn menu_holes(window: [i32; 4], menus: &[[i32; 4]]) -> Vec<[i32; 4]> {
    menus
        .iter()
        .filter_map(|m| {
            let (l, t) = (m[0].max(window[0]), m[1].max(window[1]));
            let (r, b) = (m[2].min(window[2]), m[3].min(window[3]));
            (l < r && t < b).then(|| [l - window[0], t - window[1], r - window[0], b - window[1]])
        })
        .collect()
}

/// `boxes` ([x, y, width, height] in device pixels from the top-left of a client
/// area at `client`: left, top, right, bottom on the screen) as screen rectangles,
/// cut to that client area: what lies beyond it is not drawn, so it must not be
/// cut out of the windows around it either.
pub fn menu_rects(client: [i32; 4], boxes: &[[f64; 4]]) -> Vec<[i32; 4]> {
    boxes
        .iter()
        .filter_map(|[x, y, w, h]| {
            let l = (client[0] + x.floor() as i32).max(client[0]);
            let t = (client[1] + y.floor() as i32).max(client[1]);
            let r = (client[0] + (x + w).ceil() as i32).min(client[2]);
            let b = (client[1] + (y + h).ceil() as i32).min(client[3]);
            (l < r && t < b).then_some([l, t, r, b])
        })
        .collect()
}

/// The windows that have a hole now, each with the window whose menu made it:
/// only those are restored when that menu closes or its window goes.
fn holed_windows() -> &'static Mutex<HashMap<String, String>> {
    static HOLED: std::sync::OnceLock<Mutex<HashMap<String, String>>> = std::sync::OnceLock::new();
    HOLED.get_or_init(|| Mutex::new(HashMap::new()))
}

/// Top-level windows, topmost first.
#[cfg(windows)]
fn windows_in_z_order() -> Vec<windows_sys::Win32::Foundation::HWND> {
    use windows_sys::Win32::Foundation::{BOOL, HWND, LPARAM};
    use windows_sys::Win32::UI::WindowsAndMessaging::EnumWindows;
    unsafe extern "system" fn collect(hwnd: HWND, list: LPARAM) -> BOOL {
        (*(list as *mut Vec<HWND>)).push(hwnd);
        1
    }
    let mut all: Vec<HWND> = Vec::new();
    unsafe { EnumWindows(Some(collect), &mut all as *mut Vec<HWND> as LPARAM) };
    all
}

/// Restores the windows the menus of window `source` cut holes in, and forgets
/// `source` itself: its menu cannot report any more once it has gone.
fn release_menu_holes(app: &tauri::AppHandle, source: &str) {
    let Ok(mut holed) = holed_windows().lock() else { return };
    holed.remove(source);
    let made: Vec<String> = holed
        .iter()
        .filter(|(_, by)| by.as_str() == source)
        .map(|(win, _)| win.clone())
        .collect();
    for label in made {
        holed.remove(&label);
        #[cfg(windows)]
        if let Some(Ok(handle)) = app.get_webview_window(&label).map(|w| w.hwnd()) {
            unsafe { windows_sys::Win32::Graphics::Gdi::SetWindowRgn(handle.0 as _, std::ptr::null_mut(), 1) };
        }
    }
    #[cfg(not(windows))]
    let _ = app;
}

/// `rects` are the boxes of the menu open in the page of window `label`:
/// [x, y, width, height] in device pixels from the top-left of its client area.
/// An empty list closes the holes that menu made.
#[tauri::command]
fn set_menu_area(app: tauri::AppHandle, label: String, rects: Vec<[f64; 4]>) -> Result<(), String> {
    #[cfg(windows)]
    {
        use windows_sys::Win32::Foundation::{HWND, RECT};
        use windows_sys::Win32::Graphics::Gdi::{CombineRgn, CreateRectRgn, DeleteObject, SetWindowRgn, RGN_DIFF};
        use windows_sys::Win32::UI::WindowsAndMessaging::GetWindowRect;
        let owner = app
            .get_webview_window(&label)
            .ok_or_else(|| format!("Window '{label}' not found"))?;
        let origin = owner.inner_position().map_err(|e| e.to_string())?;
        let size = owner.inner_size().map_err(|e| e.to_string())?;
        let client = [origin.x, origin.y, origin.x + size.width as i32, origin.y + size.height as i32];
        let menus = menu_rects(client, &rects);
        // Only what lies above the menu's window covers it.
        let owner_hwnd = owner.hwnd().map_err(|e| e.to_string())?.0 as HWND;
        let order = windows_in_z_order();
        let above: std::collections::HashSet<isize> = if order.contains(&owner_hwnd) {
            order.iter().take_while(|&&h| h != owner_hwnd).map(|&h| h as isize).collect()
        } else {
            Default::default()
        };
        let mut holed = holed_windows().lock().map_err(|e| e.to_string())?;
        for (other, win) in app.webview_windows() {
            if other == label {
                continue;
            }
            let Ok(handle) = win.hwnd() else { continue };
            let hwnd = handle.0 as HWND;
            let mut frame = RECT { left: 0, top: 0, right: 0, bottom: 0 };
            let holes = if !menus.is_empty()
                && above.contains(&(hwnd as isize))
                && unsafe { GetWindowRect(hwnd, &mut frame) } != 0
            {
                menu_holes([frame.left, frame.top, frame.right, frame.bottom], &menus)
            } else {
                Vec::new()
            };
            if holes.is_empty() {
                if holed.get(&other) == Some(&label) {
                    holed.remove(&other);
                    unsafe { SetWindowRgn(hwnd, std::ptr::null_mut(), 1) };
                }
                continue;
            }
            unsafe {
                let region = CreateRectRgn(0, 0, frame.right - frame.left, frame.bottom - frame.top);
                if region.is_null() {
                    continue;
                }
                for [l, t, r, b] in holes {
                    let hole = CreateRectRgn(l, t, r, b);
                    if !hole.is_null() {
                        CombineRgn(region, region, hole, RGN_DIFF);
                        DeleteObject(hole);
                    }
                }
                // On success the window owns the region; on failure it is still ours.
                if SetWindowRgn(hwnd, region, 1) == 0 {
                    DeleteObject(region);
                    continue;
                }
            }
            holed.insert(other, label.clone());
        }
        Ok(())
    }
    #[cfg(not(windows))]
    {
        let _ = (app, label, rects);
        Ok(())
    }
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
            // A window that goes cannot close its menu any more.
            if let WindowEvent::Destroyed = event {
                release_menu_holes(window.app_handle(), window.label());
            }
            // Only a close request tells the user closing a panel from Windows taking
            // it down with the main window, which must not count as hiding it.
            if window.label().starts_with("panel-") {
                if let WindowEvent::CloseRequested { .. } = event {
                    let _ = window
                        .app_handle()
                        .emit_to("main", "panel-closed", window.label());
                }
            }
            // Closing a dialog from Windows (Alt+F4) is the user's answer. The window
            // stays until the state says it goes, so its owner is enabled again first.
            if window.label().starts_with("dialog-") {
                if let WindowEvent::CloseRequested { api, .. } = event {
                    api.prevent_close();
                    let _ = window
                        .app_handle()
                        .emit_to("main", "dialog-close-requested", window.label());
                }
            }
            if window.label() == "main" {
                if let WindowEvent::Destroyed = event {
                    for (lbl, win) in window.app_handle().webview_windows() {
                        if lbl.starts_with("panel-") {
                            let _ = win.close();
                        } else if lbl.starts_with("dialog-") {
                            let _ = win.destroy();
                        }
                    }
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
            open_dialog_window,
            show_dialog_window,
            close_dialog_window,
            set_window_enabled,
            set_panels_focusable,
            focus_modal,
            set_menu_area,
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
    fn test_dialog_and_owner_labels() {
        assert_eq!(validate_dialog_label("dialog-d12"), Ok("dialog-d12"));
        assert_eq!(validate_dialog_label(" dialog-d_3 "), Ok("dialog-d_3"));
        assert!(validate_dialog_label("dialog-").is_err());
        assert!(validate_dialog_label("main").is_err());
        assert!(validate_dialog_label("panel-toolsDock").is_err());
        assert!(validate_dialog_label("dialog-../x").is_err());
        assert!(validate_dialog_label(&format!("dialog-{}", "a".repeat(33))).is_err());
        assert_eq!(validate_owner_label("main"), Ok("main"));
        assert_eq!(validate_owner_label("dialog-d1"), Ok("dialog-d1"));
        assert!(validate_owner_label("panel-toolsDock").is_err());
    }

    #[test]
    fn test_center_over_owner() {
        assert_eq!(center_over((100.0, 50.0), (1000.0, 800.0), (300.0, 200.0)), (450.0, 350.0));
        // Taller than its owner: the dialog keeps the owner's top edge, not above it.
        assert_eq!(center_over((100.0, 50.0), (400.0, 300.0), (300.0, 500.0)), (150.0, 50.0));
        assert_eq!(center_over((0.0, 0.0), (101.0, 101.0), (50.0, 50.0)), (26.0, 26.0));
    }

    #[test]
    fn test_menu_holes() {
        // A panel at (100, 50)-(300, 250) over a menu at (250, 100)-(400, 180): the
        // overlap, in the panel's own coordinates.
        let panel = [100, 50, 300, 250];
        assert_eq!(menu_holes(panel, &[[250, 100, 400, 180]]), vec![[150, 50, 200, 130]]);
        // A menu and its submenu; only the part on the panel counts.
        assert_eq!(
            menu_holes(panel, &[[120, 60, 200, 120], [200, 80, 320, 140]]),
            vec![[20, 10, 100, 70], [100, 30, 200, 90]]
        );
        // Beside the panel, or touching its edge only: no hole.
        assert!(menu_holes(panel, &[[300, 60, 400, 120]]).is_empty());
        assert!(menu_holes(panel, &[[0, 0, 100, 50]]).is_empty());
        assert!(menu_holes(panel, &[]).is_empty());
    }

    #[test]
    fn test_menu_rects() {
        // A client area at (100, 50)-(900, 650) on the screen.
        let client = [100, 50, 900, 650];
        // Device pixels from the client's top-left, rounded outwards.
        assert_eq!(menu_rects(client, &[[10.4, 20.6, 100.0, 50.2]]), vec![[110, 70, 211, 121]]);
        // A submenu running past the right edge is cut there; one wholly outside is dropped.
        assert_eq!(menu_rects(client, &[[750.0, 100.0, 200.0, 80.0]]), vec![[850, 150, 900, 230]]);
        assert_eq!(menu_rects(client, &[[780.0, 0.0, 40.0, 40.0], [0.0, 600.0, 10.0, 0.0]]), vec![[880, 50, 900, 90]]);
        assert!(menu_rects(client, &[[800.0, 0.0, 40.0, 40.0]]).is_empty());
        assert!(menu_rects(client, &[]).is_empty());
    }

    #[test]
    fn test_modal_target() {
        let main = 1;
        let win = |id, root_owner, shown, enabled, panel| TopWindow { id, root_owner, shown, enabled, panel };
        // A clicked panel has risen above the dialog: the dialog still holds the input.
        let panel_on_top = [win(5, main, true, true, true), win(7, main, true, true, false), win(main, main, true, false, false)];
        assert_eq!(modal_target(main, &panel_on_top), Some(7));
        // A dialog over a dialog: the lower one is disabled, the upper one is picked.
        let nested = [win(8, main, true, true, false), win(7, main, true, false, false), win(main, main, true, false, false)];
        assert_eq!(modal_target(main, &nested), Some(8));
        // A native picker over a dialog page; a hidden window and another program's tree are ignored.
        let picker = [
            win(9, 42, true, true, false),
            win(4, main, false, true, false),
            win(6, main, true, true, false),
            win(7, main, true, true, false),
            win(main, main, true, false, false),
        ];
        assert_eq!(modal_target(main, &picker), Some(6));
        // Nothing shown yet: no target, the caller falls back.
        assert_eq!(modal_target(main, &[win(5, main, true, true, true), win(main, main, true, false, false)]), None);
    }

    #[test]
    fn test_place_dialog_stays_on_the_monitor() {
        let area = Some(((0.0, 0.0), (1920.0, 1040.0)));
        // Over the middle of a normal owner, as before.
        assert_eq!(place_dialog(Some(((100.0, 50.0), (1000.0, 800.0))), area, (300.0, 200.0)), (450.0, 350.0));
        // A minimized owner sits near -32000: the dialog goes to the middle of its monitor.
        assert_eq!(place_dialog(None, area, (320.0, 200.0)), (800.0, 420.0));
        // An owner hanging off the right edge: the dialog is pulled back onto the monitor.
        assert_eq!(place_dialog(Some(((1800.0, 900.0), (400.0, 300.0))), area, (300.0, 200.0)), (1620.0, 840.0));
        // Bigger than the work area: the top-left corner, so the title bar stays reachable.
        assert_eq!(place_dialog(Some(((0.0, 0.0), (800.0, 600.0))), area, (2400.0, 1200.0)), (0.0, 0.0));
        // A second monitor to the left.
        let left = Some(((-1280.0, 0.0), (1280.0, 984.0)));
        assert_eq!(place_dialog(None, left, (300.0, 200.0)), (-790.0, 392.0));
        // No monitor known: the owner alone decides.
        assert_eq!(place_dialog(Some(((100.0, 50.0), (1000.0, 800.0))), None, (300.0, 200.0)), (450.0, 350.0));
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
