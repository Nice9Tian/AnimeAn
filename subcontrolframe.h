#ifndef SUBCONTROLFRAME_H
#define SUBCONTROLFRAME_H

#include <QList>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QWidget>

class QSizeGrip;
class QVBoxLayout;
class SubControlFrame;

// A surface that can adopt a SubControlFrame. Deliberately tiny: a host only
// has to answer WHERE a drop would land and HOW to take the frame in. Which
// widgets are hosts, and what a "slot" means on each of them, stays with the
// panels - the frame knows nothing about grids or button columns.
class SubControlHost
{
public:
    virtual ~SubControlHost();

    // The widget the host IS, so the frame can map coordinates and park a
    // preview overlay in it.
    virtual QWidget *subControlHostWidget() = 0;
    // The drop slot under `globalPos`, in GLOBAL coordinates. An empty rect
    // means "not over me" - never a guessed slot, so a frame released over
    // nothing simply stays floating.
    virtual QRect subControlPreviewRect(const QPoint &globalPos) const = 0;
    // Take the frame in. The host owns the placement; the frame has already
    // left wherever it was.
    virtual void embedSubControl(SubControlFrame *frame) = 0;
    // Bring an already-embedded frame to where it can be seen. Only a host
    // that can HIDE a frame it still owns has anything to do here (a stacked
    // page has to be selected first), which is why it is not pure: a host that
    // shows everything it holds is already done.
    virtual void revealSubControl(SubControlFrame *frame);
};

// The one place a frame is looked up by name and a host is found by position.
// Frames OUTLIVE the panels that show them (a panel rebuild parks its frames
// rather than deleting them), which is exactly why their lifetime lives here
// and not in a layout.
class SubControlRegistry
{
public:
    static SubControlRegistry *instance();

    void registerFrame(SubControlFrame *frame);
    void unregisterFrame(SubControlFrame *frame);
    SubControlFrame *frame(const QString &name) const;
    QList<SubControlFrame *> frames() const;

    void registerHost(SubControlHost *host);
    void unregisterHost(SubControlHost *host);
    QList<SubControlHost *> hosts() const;

    // The parking lot: a hidden, never-shown widget that keeps a frame alive
    // between homes. Reparenting to nullptr instead would flash the frame as a
    // top-level window on the way past.
    QWidget *keeper();

private:
    QList<SubControlFrame *> m_frames;
    QList<SubControlHost *> m_hosts;
    QWidget *m_keeper = nullptr;
};

// The frame's 20px title bar: the name, a float/dock glyph, and the grab
// handle the whole mechanism hangs off. It reports the drag in GLOBAL
// coordinates and decides nothing itself.
class SubControlTitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit SubControlTitleBar(QWidget *parent = nullptr);

    void setTitle(const QString &title);
    // Which way the affordance glyph points: out of the panel when embedded,
    // back into it when floating.
    void setFloating(bool floating);
    QSize sizeHint() const override;

signals:
    void dragStarted(const QPoint &globalPos);
    void dragMoved(const QPoint &globalPos);
    void dragFinished(const QPoint &globalPos);
    void affordanceClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRect affordanceRect() const;

    QString m_title;
    bool m_floating = false;
    bool m_hoverAffordance = false;
    bool m_pressedAffordance = false;
    bool m_pressed = false;
    bool m_dragging = false;
    QPoint m_pressGlobal;
};

// A titled content slot that can live INSIDE a host panel or as a floating
// tool window, and moves between the two by dragging its title bar. The frame
// carries no policy about what it contains: the shell puts a widget in the
// slot and the frame only decides where the slot currently is.
class SubControlFrame : public QWidget
{
    Q_OBJECT

public:
    enum class Placement {
        Parked,     // in the registry's keeper, waiting for a home
        Embedded,   // inside a host panel
        Floating    // its own Qt::Tool window
    };

    // `owner` is the window a floating frame belongs to (it stays on top of
    // that window and dies with it); it is also the frame's first parent.
    SubControlFrame(const QString &name, const QString &title, QWidget *owner);
    ~SubControlFrame() override;

    QString name() const;

    // Where content goes. Reparent into this and add to its layout through
    // setContent(); the frame never deletes what it was given.
    void setContent(QWidget *widget);
    QWidget *content() const;
    // Releases the content from the layout WITHOUT deleting or reparenting it:
    // the caller is taking it somewhere, exactly like PaintViewContainer's
    // chrome band.
    void releaseContent();
    // Shown in place of the content while something else owns it.
    void setPlaceholderText(const QString &text);

    Placement placement() const;
    bool isFloating() const;
    SubControlHost *host() const;
    // True when the frame is somewhere a user could actually be looking at it.
    // Deliberately not isVisible(): before the main window is first shown
    // every widget reads as invisible, and the router would then park a frame
    // the user had just asked for.
    bool isLive() const;

    void embedInto(SubControlHost *host);
    // Adopt WITHOUT asking the host to lay the frame out: a host that builds
    // the frame into a declared slot of its own (the options grid's
    // "subwindow" control) has already placed it, and only the frame's idea of
    // where it lives is still stale.
    void adoptedBy(SubControlHost *host);
    // Detaches to a floating tool window. Without a position it lands beside
    // the owner window, or back where it last floated.
    void floatFrame();
    void floatFrame(const QPoint &globalTopLeft);
    void park();
    // Show and raise wherever it is; floats it when it has no home yet.
    void surface();

signals:
    // Emitted whenever the answer to "where does this frame live" changed, so
    // whatever fills the slot can follow it.
    void homeChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void applyTheme();
    void beginDrag(const QPoint &globalPos);
    void dragTo(const QPoint &globalPos);
    void endDrag(const QPoint &globalPos);
    SubControlHost *hostAt(const QPoint &globalPos, QRect *previewRect) const;
    void showPreview(SubControlHost *host, const QRect &globalRect);
    void hidePreview();
    void detachFromHost();

    QString m_name;
    QString m_title;
    QWidget *m_owner = nullptr;
    SubControlTitleBar *m_titleBar = nullptr;
    QWidget *m_body = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;
    class QLabel *m_placeholder = nullptr;
    QWidget *m_content = nullptr;
    QSizeGrip *m_grip = nullptr;
    // The translucent Accent wash that says where a release would land. One
    // instance, reparented into whichever host is under the cursor - which is
    // also why it is a guarded pointer: it dies with whatever host it was
    // last parented to.
    QPointer<QWidget> m_preview;
    SubControlHost *m_host = nullptr;
    // Where the frame would go BACK to. Separate from m_host because floating
    // means having no host, and the dock affordance still has to answer "back
    // to where". Cleared only by park(), which is the user saying "put it
    // away" rather than "take it out".
    SubControlHost *m_lastHost = nullptr;
    Placement m_placement = Placement::Parked;
    QRect m_floatGeometry;
    QPoint m_dragGrab;      // cursor offset inside the frame when the drag began
    bool m_dragging = false;
    bool m_emittingHomeChanged = false;
};

#endif // SUBCONTROLFRAME_H
