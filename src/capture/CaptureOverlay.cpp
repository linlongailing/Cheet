#include "capture/CaptureOverlay.h"

#include "i18n/I18n.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QStyle>
#include <QToolButton>
#include <QtMath>

namespace {
constexpr QColor kAnnColor(255, 77, 79);
constexpr int kAnnWidth = 2;
constexpr int kHandleHit = 8;
constexpr int kHandleRadius = 4;
constexpr int kMinSel = 8;

QString iconPath(const QString &key)
{
    return QStringLiteral(":/qt/qml/CheetahClip/resources/icons/tools/%1.png").arg(key);
}
} // namespace

CaptureOverlay::CaptureOverlay(const QPixmap &screenshot, bool fixedMode, int fixedW, int fixedH,
                               QWidget *parent)
    : QWidget(parent)
    , m_screenshot(screenshot)
    , m_fixedMode(fixedMode)
    , m_fixedW(fixedW)
    , m_fixedH(fixedH)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);

    QRect geo;
    for (QScreen *screen : QGuiApplication::screens())
        geo = geo.united(screen->geometry());
    setGeometry(geo);

    m_toolbar = new QWidget(this);
    m_toolbar->setObjectName(QStringLiteral("captureToolbar"));
    m_toolbar->setStyleSheet(QStringLiteral(
        "QWidget#captureToolbar {"
        "  background: rgba(26,27,30,235);"
        "  border: 1px solid #3a3d45;"
        "  border-radius: 8px;"
        "}"
        "QToolButton {"
        "  background: #4a4e58;"
        "  border: 1px solid #5a5e68;"
        "  border-radius: 6px;"
        "  padding: 2px;"
        "  min-width: 26px;"
        "  max-width: 26px;"
        "  min-height: 26px;"
        "  max-height: 26px;"
        "  color: #ffffff;"
        "}"
        "QToolButton:hover { background: #5a5e68; border-color: #6a6e78; }"
        "QToolButton[active=\"true\"] {"
        "  background: #2d6cdf;"
        "  border-color: #3b7af0;"
        "}"
        "QToolButton#aiBtn {"
        "  background: #2d6cdf;"
        "  border-color: #2d6cdf;"
        "  padding: 2px 8px;"
        "  min-width: 56px;"
        "  max-width: 72px;"
        "  min-height: 26px;"
        "  max-height: 26px;"
        "  color: #ffffff;"
        "  font-size: 11px;"
        "}"
        "QToolButton#aiBtn:hover { background: #3b7af0; }"
        "QToolButton#cancelBtn {"
        "  background: #e5484d;"
        "  border-color: #e5484d;"
        "  color: #ffffff;"
        "}"
        "QToolButton#cancelBtn:hover { background: #f2555a; border-color: #f2555a; }"
        "QToolButton#okBtn {"
        "  background: #30a46c;"
        "  border-color: #30a46c;"
        "  color: #ffffff;"
        "}"
        "QToolButton#okBtn:hover { background: #3cb179; border-color: #3cb179; }"));

    auto *lay = new QHBoxLayout(m_toolbar);
    lay->setContentsMargins(6, 4, 6, 4);
    lay->setSpacing(4);

    auto &i18n = I18n::instance();
    const QStringList keys = {QStringLiteral("rect"),    QStringLiteral("ellipse"),
                              QStringLiteral("arrow"),   QStringLiteral("pen"),
                              QStringLiteral("text"),    QStringLiteral("undo"),
                              QStringLiteral("cancel"),  QStringLiteral("ok"),
                              QStringLiteral("ai")};

    for (const QString &key : keys) {
        auto *btn = new QToolButton(m_toolbar);
        btn->setAutoRaise(false);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(i18n.tr(QStringLiteral("tool_%1").arg(key)) + QLatin1Char('\n')
                        + i18n.tr(QStringLiteral("tool_%1_desc").arg(key)));
        if (key == QLatin1String("ai")) {
            // 用文案区分中英文：中文「AI 问询」、英文「AI ask」
            btn->setObjectName(QStringLiteral("aiBtn"));
            btn->setText(i18n.tr(QStringLiteral("tool_ai")));
            btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        } else {
            if (key == QLatin1String("ok"))
                btn->setObjectName(QStringLiteral("okBtn"));
            else if (key == QLatin1String("cancel"))
                btn->setObjectName(QStringLiteral("cancelBtn"));
            btn->setIcon(loadToolIcon(key));
            btn->setIconSize(QSize(16, 16));
            if (btn->icon().isNull()) {
                static const QHash<QString, QString> fallback = {
                    {QStringLiteral("rect"), QStringLiteral("□")},
                    {QStringLiteral("ellipse"), QStringLiteral("○")},
                    {QStringLiteral("arrow"), QStringLiteral("↗")},
                    {QStringLiteral("pen"), QStringLiteral("✎")},
                    {QStringLiteral("text"), QStringLiteral("T")},
                    {QStringLiteral("undo"), QStringLiteral("↩")},
                    {QStringLiteral("cancel"), QStringLiteral("✕")},
                    {QStringLiteral("ok"), QStringLiteral("✓")},
                };
                btn->setText(fallback.value(key, key));
                btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
            } else {
                btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
            }
        }

        connect(btn, &QToolButton::clicked, this, [this, key]() { onToolClicked(key); });
        lay->addWidget(btn);
        m_toolButtons.insert(key, btn);
    }

    m_toolbar->adjustSize();
    m_toolbar->hide();

    if (m_fixedMode) {
        m_origin = QPoint(width() / 2, height() / 2);
        m_current = m_origin;
        m_hasSelection = true;
        m_selecting = false;
        m_editing = true;
        m_lockedSelection = selectionRect();
        setCursor(Qt::ArrowCursor);
        showToolbar();
    }
}

QIcon CaptureOverlay::loadToolIcon(const QString &key)
{
    const QPixmap pm(iconPath(key));
    if (pm.isNull())
        return {};
    return QIcon(pm);
}

QRect CaptureOverlay::selectionRect() const
{
    if (m_editing && m_lockedSelection.isValid())
        return m_lockedSelection;
    if (m_fixedMode && m_hasSelection) {
        return QRect(m_current.x() - m_fixedW / 2, m_current.y() - m_fixedH / 2, m_fixedW, m_fixedH)
            .intersected(rect());
    }
    if (!m_hasSelection && !m_dragging)
        return {};
    return QRect(m_origin, m_current).normalized().intersected(rect());
}

QPoint CaptureOverlay::clampToSelection(const QPoint &p) const
{
    const QRect sel = selectionRect();
    if (!sel.isValid())
        return p;
    return QPoint(qBound(p.x(), sel.left(), sel.right()), qBound(p.y(), sel.top(), sel.bottom()));
}

void CaptureOverlay::showToolbar()
{
    const QRect sel = selectionRect();
    if (!sel.isValid())
        return;
    m_toolbar->adjustSize();
    // 右对齐选区右缘，优先放在选区下方
    int x = sel.right() - m_toolbar->width();
    int y = sel.bottom() + 10;
    if (y + m_toolbar->height() > height() - 8)
        y = sel.top() - m_toolbar->height() - 10;
    x = qBound(x, 8, width() - m_toolbar->width() - 8);
    y = qBound(y, 8, height() - m_toolbar->height() - 8);
    m_toolbar->move(x, y);
    m_toolbar->show();
    m_toolbar->raise();
    updateToolHighlights();
}

void CaptureOverlay::hideToolbar()
{
    m_toolbar->hide();
}

void CaptureOverlay::updateToolHighlights()
{
    const QString active = m_tool == Tool::Rect       ? QStringLiteral("rect")
                           : m_tool == Tool::Ellipse  ? QStringLiteral("ellipse")
                           : m_tool == Tool::Arrow    ? QStringLiteral("arrow")
                           : m_tool == Tool::Pen      ? QStringLiteral("pen")
                           : m_tool == Tool::Text     ? QStringLiteral("text")
                                                      : QString();
    for (auto it = m_toolButtons.begin(); it != m_toolButtons.end(); ++it) {
        const QString key = it.key();
        if (key == QLatin1String("undo") || key == QLatin1String("cancel") || key == QLatin1String("ok")
            || key == QLatin1String("ai"))
            continue;
        it.value()->setProperty("active", key == active);
        it.value()->style()->unpolish(it.value());
        it.value()->style()->polish(it.value());
    }
}

Qt::CursorShape CaptureOverlay::cursorForHandle(ResizeHandle handle)
{
    switch (handle) {
    case ResizeHandle::TopLeft:
    case ResizeHandle::BottomRight:
        return Qt::SizeFDiagCursor;
    case ResizeHandle::TopRight:
    case ResizeHandle::BottomLeft:
        return Qt::SizeBDiagCursor;
    case ResizeHandle::Top:
    case ResizeHandle::Bottom:
        return Qt::SizeVerCursor;
    case ResizeHandle::Left:
    case ResizeHandle::Right:
        return Qt::SizeHorCursor;
    default:
        return Qt::ArrowCursor;
    }
}

QPoint CaptureOverlay::handleCenter(ResizeHandle handle) const
{
    const QRect sel = selectionRect();
    const QPoint c = sel.center();
    switch (handle) {
    case ResizeHandle::TopLeft:
        return sel.topLeft();
    case ResizeHandle::Top:
        return QPoint(c.x(), sel.top());
    case ResizeHandle::TopRight:
        return sel.topRight();
    case ResizeHandle::Right:
        return QPoint(sel.right(), c.y());
    case ResizeHandle::BottomRight:
        return sel.bottomRight();
    case ResizeHandle::Bottom:
        return QPoint(c.x(), sel.bottom());
    case ResizeHandle::BottomLeft:
        return sel.bottomLeft();
    case ResizeHandle::Left:
        return QPoint(sel.left(), c.y());
    default:
        return {};
    }
}

CaptureOverlay::ResizeHandle CaptureOverlay::hitTestHandle(const QPoint &pos) const
{
    // 固定宽高模式：选区尺寸不可拖动手柄调整
    if (m_fixedMode || !m_editing || !selectionRect().isValid())
        return ResizeHandle::None;
    static const ResizeHandle handles[] = {
        ResizeHandle::TopLeft,     ResizeHandle::Top,    ResizeHandle::TopRight,
        ResizeHandle::Right,       ResizeHandle::BottomRight, ResizeHandle::Bottom,
        ResizeHandle::BottomLeft,  ResizeHandle::Left,
    };
    for (ResizeHandle h : handles) {
        const QPoint c = handleCenter(h);
        if (QRect(c.x() - kHandleHit, c.y() - kHandleHit, kHandleHit * 2, kHandleHit * 2).contains(pos))
            return h;
    }
    return ResizeHandle::None;
}

QRect CaptureOverlay::resizedSelection(ResizeHandle handle, const QPoint &pos) const
{
    QRect sel = selectionRect();
    if (!sel.isValid())
        return sel;

    const QPoint p(qBound(pos.x(), 0, width() - 1), qBound(pos.y(), 0, height() - 1));
    switch (handle) {
    case ResizeHandle::TopLeft:
        sel.setTopLeft(p);
        break;
    case ResizeHandle::Top:
        sel.setTop(p.y());
        break;
    case ResizeHandle::TopRight:
        sel.setTopRight(p);
        break;
    case ResizeHandle::Right:
        sel.setRight(p.x());
        break;
    case ResizeHandle::BottomRight:
        sel.setBottomRight(p);
        break;
    case ResizeHandle::Bottom:
        sel.setBottom(p.y());
        break;
    case ResizeHandle::BottomLeft:
        sel.setBottomLeft(p);
        break;
    case ResizeHandle::Left:
        sel.setLeft(p.x());
        break;
    default:
        return sel;
    }

    sel = sel.normalized();
    if (sel.width() < kMinSel) {
        if (handle == ResizeHandle::Left || handle == ResizeHandle::TopLeft
            || handle == ResizeHandle::BottomLeft)
            sel.setLeft(sel.right() - kMinSel + 1);
        else if (handle == ResizeHandle::Right || handle == ResizeHandle::TopRight
                 || handle == ResizeHandle::BottomRight)
            sel.setRight(sel.left() + kMinSel - 1);
        else
            sel.setWidth(kMinSel);
    }
    if (sel.height() < kMinSel) {
        if (handle == ResizeHandle::Top || handle == ResizeHandle::TopLeft
            || handle == ResizeHandle::TopRight)
            sel.setTop(sel.bottom() - kMinSel + 1);
        else if (handle == ResizeHandle::Bottom || handle == ResizeHandle::BottomLeft
                 || handle == ResizeHandle::BottomRight)
            sel.setBottom(sel.top() + kMinSel - 1);
        else
            sel.setHeight(kMinSel);
    }
    return sel.intersected(rect());
}

QRect CaptureOverlay::movedSelection(const QPoint &delta) const
{
    QRect sel = m_moveStartSel.translated(delta);
    if (sel.left() < 0)
        sel.translate(-sel.left(), 0);
    if (sel.top() < 0)
        sel.translate(0, -sel.top());
    if (sel.right() >= width())
        sel.translate(width() - 1 - sel.right(), 0);
    if (sel.bottom() >= height())
        sel.translate(0, height() - 1 - sel.bottom());
    return sel;
}

void CaptureOverlay::offsetAnnotations(const QPoint &delta)
{
    if (delta.isNull())
        return;
    for (Annotation &a : m_annotations) {
        a.p1 += delta;
        a.p2 += delta;
        for (QPoint &pt : a.path)
            pt += delta;
    }
    m_textPos += delta;
    if (m_textEntry && m_textEntry->isVisible())
        m_textEntry->move(m_textEntry->pos() + delta);
}

void CaptureOverlay::updateCursor()
{
    if (!m_editing) {
        setCursor(Qt::CrossCursor);
        return;
    }
    if (m_resizing || m_hoverHandle != ResizeHandle::None) {
        setCursor(cursorForHandle(m_resizing ? m_activeHandle : m_hoverHandle));
        return;
    }
    if (m_moving || m_tool == Tool::None) {
        // 未选标注工具时：选区内为四向移动光标
        if (m_moving || m_pointerInSelection)
            setCursor(Qt::SizeAllCursor);
        else
            setCursor(Qt::ArrowCursor);
        return;
    }
    switch (m_tool) {
    case Tool::Text:
        setCursor(Qt::IBeamCursor);
        break;
    case Tool::Pen:
        setCursor(Qt::CrossCursor);
        break;
    default:
        setCursor(Qt::CrossCursor);
        break;
    }
}

void CaptureOverlay::setTool(Tool tool)
{
    m_tool = tool;
    updateToolHighlights();
    updateCursor();
}

void CaptureOverlay::onToolClicked(const QString &key)
{
    commitTextEntry();
    if (key == QLatin1String("undo")) {
        undoLast();
        return;
    }
    if (key == QLatin1String("ok")) {
        confirm(false);
        return;
    }
    if (key == QLatin1String("ai")) {
        confirm(true);
        return;
    }
    if (key == QLatin1String("cancel")) {
        emit cancelled();
        return;
    }

    Tool next = Tool::None;
    if (key == QLatin1String("rect"))
        next = Tool::Rect;
    else if (key == QLatin1String("ellipse"))
        next = Tool::Ellipse;
    else if (key == QLatin1String("arrow"))
        next = Tool::Arrow;
    else if (key == QLatin1String("pen"))
        next = Tool::Pen;
    else if (key == QLatin1String("text"))
        next = Tool::Text;

    setTool(m_tool == next ? Tool::None : next);
}

void CaptureOverlay::undoLast()
{
    if (m_annotations.isEmpty())
        return;
    m_annotations.removeLast();
    update();
}

void CaptureOverlay::placeTextEntry(const QPoint &pos)
{
    const QRect sel = selectionRect();
    if (!sel.isValid() || !sel.contains(pos))
        return;

    commitTextEntry();
    m_textPos = pos;
    if (!m_textEntry) {
        m_textEntry = new QLineEdit(this);
        m_textEntry->setFrame(false);
        m_textEntry->setAttribute(Qt::WA_TranslucentBackground);
        m_textEntry->setAutoFillBackground(false);
        m_textEntry->setStyleSheet(QStringLiteral(
            "QLineEdit {"
            "  background: transparent;"
            "  color: #ff4d4f;"
            "  border: none;"
            "  padding: 0;"
            "  margin: 0;"
            "  font-family: 'Microsoft YaHei UI';"
            "  font-size: 14px;"
            "  selection-background-color: rgba(255,77,79,80);"
            "  selection-color: #ff4d4f;"
            "}"));
        connect(m_textEntry, &QLineEdit::returnPressed, this, &CaptureOverlay::commitTextEntry);
        connect(m_textEntry, &QLineEdit::editingFinished, this, &CaptureOverlay::commitTextEntry);
    }
    m_textEntry->setText({});
    // 限制输入框不超出选区
    const int maxW = qMax(40, sel.right() - pos.x() - 2);
    const int h = 20;
    m_textEntry->setFixedHeight(h);
    m_textEntry->setMinimumWidth(qMin(80, maxW));
    m_textEntry->resize(qMin(160, maxW), h);
    m_textEntry->move(pos);
    m_textEntry->show();
    m_textEntry->raise();
    m_textEntry->setFocus();
}

void CaptureOverlay::commitTextEntry()
{
    if (!m_textEntry || !m_textEntry->isVisible())
        return;
    const QString text = m_textEntry->text().trimmed();
    m_textEntry->hide();
    if (text.isEmpty())
        return;
    const QRect sel = selectionRect();
    if (!sel.isValid() || !sel.contains(m_textPos))
        return;
    Annotation a;
    a.type = Tool::Text;
    a.p1 = m_textPos;
    a.text = text;
    m_annotations.push_back(a);
    update();
}

void CaptureOverlay::drawArrow(QPainter &p, const QPoint &from, const QPoint &to) const
{
    p.drawLine(from, to);
    const qreal angle = qAtan2(qreal(to.y() - from.y()), qreal(to.x() - from.x()));
    const qreal size = 12.0;
    constexpr qreal kPi = 3.14159265358979323846;
    const QPointF left(to.x() - size * qCos(angle - kPi / 6), to.y() - size * qSin(angle - kPi / 6));
    const QPointF right(to.x() - size * qCos(angle + kPi / 6), to.y() - size * qSin(angle + kPi / 6));
    QPainterPath head;
    head.moveTo(to);
    head.lineTo(left);
    head.lineTo(right);
    head.closeSubpath();
    p.fillPath(head, kAnnColor);
}

void CaptureOverlay::drawAnnotation(QPainter &p, const Annotation &a, const QPoint &origin) const
{
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(kAnnColor, kAnnWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    auto map = [&](const QPoint &pt) { return pt - origin; };

    switch (a.type) {
    case Tool::Rect:
        p.drawRect(QRect(map(a.p1), map(a.p2)).normalized());
        break;
    case Tool::Ellipse:
        p.drawEllipse(QRect(map(a.p1), map(a.p2)).normalized());
        break;
    case Tool::Arrow:
        drawArrow(p, map(a.p1), map(a.p2));
        break;
    case Tool::Pen: {
        if (a.path.size() < 2)
            break;
        QPainterPath path;
        path.moveTo(map(a.path.first()));
        for (int i = 1; i < a.path.size(); ++i)
            path.lineTo(map(a.path.at(i)));
        p.drawPath(path);
        break;
    }
    case Tool::Text:
        p.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 14));
        p.drawText(map(a.p1), a.text);
        break;
    default:
        break;
    }
}

QPixmap CaptureOverlay::renderResult() const
{
    const QRect sel = selectionRect();
    if (sel.width() < 2 || sel.height() < 2)
        return {};
    QPixmap out = m_screenshot.copy(sel);
    QPainter p(&out);
    for (const Annotation &a : m_annotations)
        drawAnnotation(p, a, sel.topLeft());
    return out;
}

void CaptureOverlay::confirm(bool sendAi)
{
    commitTextEntry();
    const QPixmap shot = renderResult();
    if (shot.isNull()) {
        emit cancelled();
        return;
    }
    emit finished(shot, sendAi);
}

void CaptureOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.drawPixmap(0, 0, m_screenshot);
    p.fillRect(rect(), QColor(0, 0, 0, 120));

    const QRect sel = selectionRect();
    if (sel.isValid() && sel.width() > 1 && sel.height() > 1) {
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawPixmap(sel, m_screenshot, sel);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setPen(QPen(QColor(QStringLiteral("#3dce7c")), 2));
        p.drawRect(sel.adjusted(0, 0, -1, -1));

        // 自由框选：四角 + 边中点白色调整手柄；固定宽高模式不显示、不可拖
        if (m_editing && !m_fixedMode) {
            static const ResizeHandle handles[] = {
                ResizeHandle::TopLeft,     ResizeHandle::Top,    ResizeHandle::TopRight,
                ResizeHandle::Right,       ResizeHandle::BottomRight, ResizeHandle::Bottom,
                ResizeHandle::BottomLeft,  ResizeHandle::Left,
            };
            p.setPen(QPen(QColor(60, 60, 60), 1));
            p.setBrush(Qt::white);
            for (ResizeHandle h : handles) {
                const QPoint c = handleCenter(h);
                p.drawEllipse(c, kHandleRadius, kHandleRadius);
            }
        }

        p.setPen(Qt::white);
        p.drawText(sel.topLeft() + QPoint(6, -8),
                   QStringLiteral("%1 × %2").arg(sel.width()).arg(sel.height()));

        for (const Annotation &a : m_annotations)
            drawAnnotation(p, a);

        if (m_drawing) {
            Annotation draft;
            draft.type = m_tool;
            draft.p1 = m_drawStart;
            draft.p2 = m_drawCurrent;
            draft.path = m_penPath;
            drawAnnotation(p, draft);
        }
    }

    if (!m_editing && !m_hasSelection) {
        auto &i18n = I18n::instance();
        p.setPen(QColor(255, 255, 255, 200));
        p.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 12));
        p.drawText(QPoint(24, 36), i18n.tr(QStringLiteral("capture_tip")));
    }
}

void CaptureOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (m_toolbar->isVisible() && m_toolbar->geometry().contains(event->pos()))
        return;
    if (m_textEntry && m_textEntry->isVisible() && m_textEntry->geometry().contains(event->pos()))
        return;

    if (m_editing) {
        const ResizeHandle handle = hitTestHandle(event->pos());
        if (handle != ResizeHandle::None) {
            commitTextEntry();
            m_resizing = true;
            m_activeHandle = handle;
            m_hoverHandle = handle;
            updateCursor();
            return;
        }
        // 未选标注工具：选区内拖拽移动整块选区（固定/自由模式均可）
        if (m_tool == Tool::None) {
            if (!selectionRect().contains(event->pos()))
                return;
            commitTextEntry();
            m_moving = true;
            m_moveOrigin = event->pos();
            m_moveStartSel = selectionRect();
            updateCursor();
            return;
        }
        // 文字只能点在选区内；点在外面忽略（不再吸附到边缘）
        if (m_tool == Tool::Text) {
            if (!selectionRect().contains(event->pos()))
                return;
            placeTextEntry(event->pos());
            return;
        }
        if (!selectionRect().contains(event->pos()))
            return;
        const QPoint pos = clampToSelection(event->pos());
        m_drawing = true;
        m_drawStart = pos;
        m_drawCurrent = pos;
        m_penPath.clear();
        if (m_tool == Tool::Pen)
            m_penPath.push_back(pos);
        update();
        return;
    }

    // 重新框选
    commitTextEntry();
    hideToolbar();
    m_annotations.clear();
    m_selecting = true;
    m_editing = false;
    m_lockedSelection = {};
    m_dragging = true;
    m_origin = event->pos();
    m_current = m_origin;
    m_hasSelection = m_fixedMode;
    setTool(Tool::None);
    setCursor(Qt::CrossCursor);
    update();
}

void CaptureOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (m_editing) {
        if (m_resizing) {
            m_lockedSelection = resizedSelection(m_activeHandle, event->pos());
            showToolbar();
            update();
            return;
        }
        if (m_moving) {
            const QRect prev = m_lockedSelection;
            m_lockedSelection = movedSelection(event->pos() - m_moveOrigin);
            offsetAnnotations(m_lockedSelection.topLeft() - prev.topLeft());
            showToolbar();
            update();
            return;
        }
        if (m_drawing) {
            m_drawCurrent = clampToSelection(event->pos());
            if (m_tool == Tool::Pen)
                m_penPath.push_back(m_drawCurrent);
            update();
            return;
        }
        const ResizeHandle hover = hitTestHandle(event->pos());
        const bool inSel = selectionRect().contains(event->pos());
        if (hover != m_hoverHandle || inSel != m_pointerInSelection) {
            m_hoverHandle = hover;
            m_pointerInSelection = inSel;
            updateCursor();
        }
        return;
    }

    m_current = event->pos();
    if (m_fixedMode) {
        m_hasSelection = true;
        update();
        return;
    }
    if (m_dragging)
        update();
}

void CaptureOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (m_editing && m_resizing) {
        m_lockedSelection = resizedSelection(m_activeHandle, event->pos());
        m_resizing = false;
        m_activeHandle = ResizeHandle::None;
        m_hoverHandle = hitTestHandle(event->pos());
        showToolbar();
        updateCursor();
        update();
        return;
    }

    if (m_editing && m_moving) {
        const QRect prev = m_lockedSelection;
        m_lockedSelection = movedSelection(event->pos() - m_moveOrigin);
        offsetAnnotations(m_lockedSelection.topLeft() - prev.topLeft());
        m_moving = false;
        showToolbar();
        updateCursor();
        update();
        return;
    }

    if (m_editing && m_drawing) {
        m_drawing = false;
        m_drawCurrent = clampToSelection(event->pos());
        if (m_tool == Tool::Pen) {
            if (m_penPath.size() >= 2) {
                Annotation a;
                a.type = Tool::Pen;
                a.path = m_penPath;
                m_annotations.push_back(a);
            }
            m_penPath.clear();
            update();
            return;
        }
        if (m_tool != Tool::Arrow && (m_drawCurrent - m_drawStart).manhattanLength() < 3) {
            update();
            return;
        }
        Annotation a;
        a.type = m_tool;
        a.p1 = m_drawStart;
        a.p2 = m_drawCurrent;
        m_annotations.push_back(a);
        update();
        return;
    }

    if (!m_dragging)
        return;
    m_dragging = false;
    m_current = event->pos();
    m_hasSelection = true;
    if (selectionRect().width() >= 8 && selectionRect().height() >= 8) {
        m_selecting = false;
        m_editing = true;
        m_lockedSelection = selectionRect();
        setCursor(Qt::ArrowCursor);
        showToolbar();
    }
    update();
}

void CaptureOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_textEntry && m_textEntry->isVisible()) {
            m_textEntry->hide();
            return;
        }
        emit cancelled();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && m_editing && !(m_textEntry && m_textEntry->isVisible())) {
        confirm(false);
        return;
    }
    if (event->matches(QKeySequence::Undo) && m_editing) {
        undoLast();
        return;
    }
}
