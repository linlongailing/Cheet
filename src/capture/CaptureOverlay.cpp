#include "capture/CaptureOverlay.h"

#include "i18n/I18n.h"

#include <QCursor>
#include <QFontMetrics>
#include <QFrame>
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
#include <QSvgRenderer>
#include <QToolButton>
#include <QtMath>

namespace {
constexpr QColor kAnnColor(255, 77, 79);
constexpr int kAnnWidth = 2;
constexpr int kAnnTextSize = 22; // 文字标注字号（输入框与绘制共用）
constexpr int kHandleHit = 8;
constexpr int kHandleRadius = 4;
constexpr int kMinSel = 8;

constexpr int kToolIconSize = 22; // 工具栏图标逻辑尺寸（与 setIconSize 一致）
constexpr int kCursorSize = 32;   // 准星光标逻辑尺寸
constexpr QColor kActiveIconColor(0x4c, 0x9b, 0xff);

QString iconPath(const QString& key) {
    return QStringLiteral(":/qt/qml/CheetahClip/resources/icons/tools/%1.svg").arg(key);
}

/** 把 SVG 渲染成 px×px 物理像素位图；tint 有效时整体染成该颜色 */
QPixmap renderSvg(const QString& path, int px, const QColor& tint = QColor()) {
    QSvgRenderer renderer(path);
    if (!renderer.isValid() || px <= 0)
        return {};
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    renderer.render(&p);
    if (tint.isValid()) {
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(pm.rect(), tint);
    }
    p.end();
    return pm;
}

/** 点到线段的最短距离（标注边框命中判定用） */
qreal distToSegment(const QPointF& p, const QPointF& a, const QPointF& b) {
    const QPointF ab = b - a;
    const qreal len2 = QPointF::dotProduct(ab, ab);
    if (len2 <= 0.0)
        return QLineF(p, a).length();
    const qreal t = qBound(0.0, QPointF::dotProduct(p - a, ab) / len2, 1.0);
    return QLineF(p, a + ab * t).length();
}

/** 箭头头部两个端点（与 drawArrow 的画法保持一致） */
void arrowHeadPoints(const QPoint& from, const QPoint& to, QPointF& left, QPointF& right) {
    const qreal angle = qAtan2(qreal(to.y() - from.y()), qreal(to.x() - from.x()));
    const qreal size = 12.0;
    constexpr qreal kPi = 3.14159265358979323846;
    left = QPointF(to.x() - size * qCos(angle - kPi / 6), to.y() - size * qSin(angle - kPi / 6));
    right = QPointF(to.x() - size * qCos(angle + kPi / 6), to.y() - size * qSin(angle + kPi / 6));
}

/** 截图准星光标（SVG 按主屏缩放比渲染，热点在正中心）；加载失败回退系统十字 */
QCursor crosshairCursor() {
    static const QCursor cursor = []() {
        const QScreen* screen = QGuiApplication::primaryScreen();
        const qreal dpr = screen ? screen->devicePixelRatio() : 1.0;
        QPixmap pm = renderSvg(QStringLiteral(":/qt/qml/CheetahClip/resources/icons/crosshair.svg"),
                               qRound(kCursorSize * dpr));
        if (pm.isNull())
            return QCursor(Qt::CrossCursor);
        pm.setDevicePixelRatio(dpr);
        return QCursor(pm); // 热点默认取中心
    }();
    return cursor;
}
} // namespace

CaptureOverlay::CaptureOverlay(const QPixmap& screenshot,
                               bool fixedMode,
                               int fixedW,
                               int fixedH,
                               QWidget* parent)
    : QWidget(parent), m_screenshot(screenshot), m_fixedMode(fixedMode), m_fixedW(fixedW),
      m_fixedH(fixedH) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setCursor(crosshairCursor());
    setFocusPolicy(Qt::StrongFocus);

    QRect geo;
    for (QScreen* screen : QGuiApplication::screens())
        geo = geo.united(screen->geometry());
    setGeometry(geo);

    m_toolbar = new QWidget(this);
    m_toolbar->setObjectName(QStringLiteral("captureToolbar"));
    m_toolbar->setStyleSheet(
        QStringLiteral("QWidget#captureToolbar {"
                       "  background: rgba(38,41,51,240);"
                       "  border-radius: 6px;"
                       "}"
                       "QToolButton {"
                       "  background: transparent;"
                       "  border: none;"
                       "  border-radius: 4px;"
                       "  padding: 0px;"
                       "  min-width: 34px;"
                       "  max-width: 34px;"
                       "  min-height: 34px;"
                       "  max-height: 34px;"
                       "  color: #e8e8ea;"
                       "  font-size: 16px;"
                       "}"
                       "QToolButton:hover { background: rgba(255,255,255,28); }"
                       "QToolButton[active=\"true\"] { background: rgba(59,130,246,60); }"
                       "QToolButton#aiBtn {"
                       "  padding: 0px 6px;"
                       "  min-width: 0px;"
                       "  max-width: 120px;"
                       "  color: #4c9bff;"
                       "  font-size: 13px;"
                       "}"
                       "QFrame#toolbarSep { background: #4a4e5a; border: none; }"));

    auto* lay = new QHBoxLayout(m_toolbar);
    lay->setContentsMargins(10, 5, 10, 5);
    lay->setSpacing(6);

    auto addSeparator = [this, lay]() {
        auto* sep = new QFrame(m_toolbar);
        sep->setObjectName(QStringLiteral("toolbarSep"));
        sep->setFixedSize(1, 22);
        lay->addSpacing(4);
        lay->addWidget(sep);
        lay->addSpacing(4);
    };

    auto& i18n = I18n::instance();
    const QStringList keys = {QStringLiteral("rect"),
                              QStringLiteral("ellipse"),
                              QStringLiteral("arrow"),
                              QStringLiteral("pen"),
                              QStringLiteral("text"),
                              QStringLiteral("undo"),
                              QStringLiteral("ai"),
                              QStringLiteral("cancel"),
                              QStringLiteral("ok")};

    for (const QString& key : keys) {
        if (key == QLatin1String("ai") || key == QLatin1String("cancel"))
            addSeparator();
        auto* btn = new QToolButton(m_toolbar);
        btn->setAutoRaise(false);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(i18n.tr(QStringLiteral("tool_%1").arg(key)) + QLatin1Char('\n') +
                        i18n.tr(QStringLiteral("tool_%1_desc").arg(key)));
        if (key == QLatin1String("ai")) {
            // 用文案区分中英文：中文「AI 问询」、英文「AI ask」
            btn->setObjectName(QStringLiteral("aiBtn"));
            btn->setText(i18n.tr(QStringLiteral("tool_ai")));
            btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        } else {
            btn->setIcon(loadToolIcon(key, false));
            btn->setIconSize(QSize(kToolIconSize, kToolIconSize));
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

    m_mousePos = mapFromGlobal(QCursor::pos());
    m_hasMousePos = rect().contains(m_mousePos);

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

QIcon CaptureOverlay::loadToolIcon(const QString& key, bool active) {
    // 按常见缩放比各渲染一份，QIcon 会挑最接近的，任何缩放下都清晰
    const QColor tint = active ? kActiveIconColor : QColor();
    QIcon icon;
    for (const qreal scale : {1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0, 4.0}) {
        const QPixmap pm = renderSvg(iconPath(key), qRound(kToolIconSize * scale), tint);
        if (pm.isNull())
            return {};
        icon.addPixmap(pm);
    }
    return icon;
}

QRect CaptureOverlay::selectionRect() const {
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

QPoint CaptureOverlay::clampToSelection(const QPoint& p) const {
    const QRect sel = selectionRect();
    if (!sel.isValid())
        return p;
    return QPoint(qBound(p.x(), sel.left(), sel.right()), qBound(p.y(), sel.top(), sel.bottom()));
}

void CaptureOverlay::showToolbar() {
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

void CaptureOverlay::hideToolbar() {
    m_toolbar->hide();
}

void CaptureOverlay::updateToolHighlights() {
    const QString active = m_tool == Tool::Rect      ? QStringLiteral("rect")
                           : m_tool == Tool::Ellipse ? QStringLiteral("ellipse")
                           : m_tool == Tool::Arrow   ? QStringLiteral("arrow")
                           : m_tool == Tool::Pen     ? QStringLiteral("pen")
                           : m_tool == Tool::Text    ? QStringLiteral("text")
                                                     : QString();
    for (auto it = m_toolButtons.begin(); it != m_toolButtons.end(); ++it) {
        const QString& key = it.key();
        if (key == QLatin1String("undo") || key == QLatin1String("cancel") ||
            key == QLatin1String("ok") || key == QLatin1String("ai"))
            continue;
        it.value()->setProperty("active", key == active);
        it.value()->setIcon(loadToolIcon(key, key == active));
        it.value()->style()->unpolish(it.value());
        it.value()->style()->polish(it.value());
    }
}

Qt::CursorShape CaptureOverlay::cursorForHandle(ResizeHandle handle) {
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

QPoint CaptureOverlay::handleCenter(ResizeHandle handle) const {
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

CaptureOverlay::ResizeHandle CaptureOverlay::hitTestHandle(const QPoint& pos) const {
    // 固定宽高模式：选区尺寸不可拖动手柄调整
    if (m_fixedMode || !m_editing || !selectionRect().isValid())
        return ResizeHandle::None;

    // 整条边线均可拖拽调整（不再只限于边中点/角上的手柄点）：
    // 鼠标落在某条边线 ±kHandleHit 范围内即命中该边，角部优先按角命中
    const QRect sel = selectionRect();
    const bool nearLeft = qAbs(pos.x() - sel.left()) <= kHandleHit;
    const bool nearRight = qAbs(pos.x() - sel.right()) <= kHandleHit;
    const bool nearTop = qAbs(pos.y() - sel.top()) <= kHandleHit;
    const bool nearBottom = qAbs(pos.y() - sel.bottom()) <= kHandleHit;
    const bool withinX = pos.x() >= sel.left() - kHandleHit && pos.x() <= sel.right() + kHandleHit;
    const bool withinY = pos.y() >= sel.top() - kHandleHit && pos.y() <= sel.bottom() + kHandleHit;

    // 四角优先
    if (nearLeft && nearTop)
        return ResizeHandle::TopLeft;
    if (nearRight && nearTop)
        return ResizeHandle::TopRight;
    if (nearLeft && nearBottom)
        return ResizeHandle::BottomLeft;
    if (nearRight && nearBottom)
        return ResizeHandle::BottomRight;
    // 四条整边
    if (nearTop && withinX)
        return ResizeHandle::Top;
    if (nearBottom && withinX)
        return ResizeHandle::Bottom;
    if (nearLeft && withinY)
        return ResizeHandle::Left;
    if (nearRight && withinY)
        return ResizeHandle::Right;
    return ResizeHandle::None;
}

QRect CaptureOverlay::resizedSelection(ResizeHandle handle, const QPoint& pos) const {
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
        if (handle == ResizeHandle::Left || handle == ResizeHandle::TopLeft ||
            handle == ResizeHandle::BottomLeft)
            sel.setLeft(sel.right() - kMinSel + 1);
        else if (handle == ResizeHandle::Right || handle == ResizeHandle::TopRight ||
                 handle == ResizeHandle::BottomRight)
            sel.setRight(sel.left() + kMinSel - 1);
        else
            sel.setWidth(kMinSel);
    }
    if (sel.height() < kMinSel) {
        if (handle == ResizeHandle::Top || handle == ResizeHandle::TopLeft ||
            handle == ResizeHandle::TopRight)
            sel.setTop(sel.bottom() - kMinSel + 1);
        else if (handle == ResizeHandle::Bottom || handle == ResizeHandle::BottomLeft ||
                 handle == ResizeHandle::BottomRight)
            sel.setBottom(sel.top() + kMinSel - 1);
        else
            sel.setHeight(kMinSel);
    }
    return sel.intersected(rect());
}

QRect CaptureOverlay::movedSelection(const QPoint& delta) const {
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

void CaptureOverlay::offsetAnnotations(const QPoint& delta) {
    if (delta.isNull())
        return;
    for (Annotation& a : m_annotations) {
        a.p1 += delta;
        a.p2 += delta;
        for (QPoint& pt : a.path)
            pt += delta;
    }
    m_textPos += delta;
    if (m_textEntry && m_textEntry->isVisible())
        m_textEntry->move(m_textEntry->pos() + delta);
}

void CaptureOverlay::updateCursor() {
    if (!m_editing) {
        setCursor(crosshairCursor());
        return;
    }
    // 正在画标注（矩形/椭圆/箭头/画笔）时使用系统箭头，与选工具后的悬停状态一致
    if (m_drawing) {
        setCursor(Qt::ArrowCursor);
        return;
    }
    if (m_resizing || m_hoverHandle != ResizeHandle::None) {
        setCursor(cursorForHandle(m_resizing ? m_activeHandle : m_hoverHandle));
        return;
    }
    // 悬停在已画标注边框上（或正在拖动它）：四向移动光标，提示可拖拽
    if (m_movingAnnIndex >= 0 || m_hoverAnnIndex >= 0) {
        setCursor(Qt::SizeAllCursor);
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
    default:
        // 矩形/椭圆/箭头/画笔：选工具后在选区内画图使用系统箭头
        setCursor(Qt::ArrowCursor);
        break;
    }
}

void CaptureOverlay::setTool(Tool tool) {
    m_tool = tool;
    if (tool == Tool::None)
        m_hoverAnnIndex = -1;
    updateToolHighlights();
    updateCursor();
}

void CaptureOverlay::onToolClicked(const QString& key) {
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

void CaptureOverlay::undoLast() {
    if (m_annotations.isEmpty())
        return;
    m_annotations.removeLast();
    update();
}

void CaptureOverlay::placeTextEntry(const QPoint& pos) {
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
        m_textEntry->setStyleSheet(
            QStringLiteral("QLineEdit {"
                           "  background: transparent;"
                           "  color: #ff4d4f;"
                           "  border: none;"
                           "  padding: 0;"
                           "  margin: 0;"
                           "  font-family: 'Microsoft YaHei UI';"
                           "  font-size: 22px;"
                           "  selection-background-color: rgba(255,77,79,80);"
                           "  selection-color: #ff4d4f;"
                           "}"));
        connect(m_textEntry, &QLineEdit::returnPressed, this, &CaptureOverlay::commitTextEntry);
        connect(m_textEntry, &QLineEdit::editingFinished, this, &CaptureOverlay::commitTextEntry);
    }
    m_textEntry->setText({});
    // 限制输入框不超出选区
    const int maxW = qMax(40, sel.right() - pos.x() - 2);
    const int h = 30;
    m_textEntry->setFixedHeight(h);
    m_textEntry->setMinimumWidth(qMin(80, maxW));
    m_textEntry->resize(qMin(160, maxW), h);
    m_textEntry->move(pos);
    m_textEntry->show();
    m_textEntry->raise();
    m_textEntry->setFocus();
}

void CaptureOverlay::commitTextEntry() {
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

QRect CaptureOverlay::annotationBounds(const Annotation& a) const {
    switch (a.type) {
    case Tool::Rect:
    case Tool::Ellipse:
        return QRect(a.p1, a.p2).normalized();
    case Tool::Arrow: {
        QPointF left;
        QPointF right;
        arrowHeadPoints(a.p1, a.p2, left, right);
        QRectF r(QPointF(a.p1), QPointF(a.p2));
        r = r.united(QRectF(left, right));
        return r.normalized().toAlignedRect().adjusted(-2, -2, 2, 2);
    }
    case Tool::Text: {
        const QFontMetrics fm(QFont(QStringLiteral("Microsoft YaHei UI"), kAnnTextSize));
        return fm.boundingRect(a.text).translated(a.p1).adjusted(-2, -2, 2, 2);
    }
    case Tool::Pen: {
        if (a.path.isEmpty())
            return {};
        QRect r(a.path.first(), a.path.first());
        for (const QPoint& pt : a.path)
            r = r.united(QRect(pt, pt));
        return r.adjusted(-2, -2, 2, 2);
    }
    default:
        return {};
    }
}

int CaptureOverlay::hitTestAnnotation(const QPoint& pos) const {
    // 边框命中容差：鼠标距图形边线 6px 内算命中（文字按文字区域算）
    constexpr qreal kTol = 6.0;
    const QPointF p = pos;
    // 从最后画的（最上层）开始找
    for (int i = m_annotations.size() - 1; i >= 0; --i) {
        const Annotation& a = m_annotations.at(i);
        switch (a.type) {
        case Tool::Rect: {
            const QRectF r = QRectF(QRect(a.p1, a.p2).normalized());
            const qreal d = qMin(qMin(distToSegment(p, r.topLeft(), r.topRight()),
                                      distToSegment(p, r.topRight(), r.bottomRight())),
                                 qMin(distToSegment(p, r.bottomRight(), r.bottomLeft()),
                                      distToSegment(p, r.bottomLeft(), r.topLeft())));
            if (d <= kTol)
                return i;
            break;
        }
        case Tool::Ellipse: {
            const QRectF r = QRectF(QRect(a.p1, a.p2).normalized());
            const qreal rx = r.width() / 2.0;
            const qreal ry = r.height() / 2.0;
            if (rx < 1.0 || ry < 1.0)
                break;
            const QPointF c = r.center();
            const qreal nx = (p.x() - c.x()) / rx;
            const qreal ny = (p.y() - c.y()) / ry;
            // 归一化半径与 1 的差换算成近似像素距离
            const qreal dist = qAbs(qSqrt(nx * nx + ny * ny) - 1.0) * qMin(rx, ry);
            if (dist <= kTol)
                return i;
            break;
        }
        case Tool::Arrow: {
            QPointF left;
            QPointF right;
            arrowHeadPoints(a.p1, a.p2, left, right);
            const qreal d = qMin(distToSegment(p, QPointF(a.p1), QPointF(a.p2)),
                                 qMin(distToSegment(p, QPointF(a.p2), left),
                                      distToSegment(p, QPointF(a.p2), right)));
            if (d <= kTol)
                return i;
            break;
        }
        case Tool::Text:
            if (annotationBounds(a).contains(pos))
                return i;
            break;
        case Tool::Pen: {
            // 自由线条：鼠标距任意一段路径 6px 内算命中
            if (a.path.size() == 1) {
                if (QLineF(p, QPointF(a.path.first())).length() <= kTol)
                    return i;
                break;
            }
            for (int j = 1; j < a.path.size(); ++j) {
                if (distToSegment(p, QPointF(a.path.at(j - 1)), QPointF(a.path.at(j))) <= kTol)
                    return i;
            }
            break;
        }
        default:
            break;
        }
    }
    return -1;
}

void CaptureOverlay::drawArrow(QPainter& p, const QPoint& from, const QPoint& to) const {
    p.drawLine(from, to);
    QPointF left;
    QPointF right;
    arrowHeadPoints(from, to, left, right);
    QPainterPath head;
    head.moveTo(to);
    head.lineTo(left);
    head.lineTo(right);
    head.closeSubpath();
    p.fillPath(head, kAnnColor);
}

void CaptureOverlay::drawAnnotation(QPainter& p, const Annotation& a, const QPoint& origin) const {
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(kAnnColor, kAnnWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    auto map = [&](const QPoint& pt) { return pt - origin; };

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
        p.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), kAnnTextSize));
        p.drawText(map(a.p1), a.text);
        break;
    default:
        break;
    }
}

QPixmap CaptureOverlay::renderResult() const {
    const QRect sel = selectionRect();
    if (sel.width() < 2 || sel.height() < 2)
        return {};
    // copy() 在带 DPR 时使用逻辑坐标裁剪，结果保留物理像素与 devicePixelRatio
    QPixmap out = m_screenshot.copy(sel);
    if (out.isNull())
        return {};
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (const Annotation& a : m_annotations)
        drawAnnotation(p, a, sel.topLeft());
    return out;
}

void CaptureOverlay::confirm(bool sendAi) {
    commitTextEntry();
    const QPixmap shot = renderResult();
    if (shot.isNull()) {
        emit cancelled();
        return;
    }
    emit finished(shot, sendAi);
}

void CaptureOverlay::drawMagnifierInfo(QPainter& p) const {
    if (!m_hasMousePos || m_screenshot.isNull())
        return;

    constexpr int kZoom = 5;
    constexpr int kSrc = 19; // 19×19 源像素 ×5 = 95×95 放大区
    constexpr int kMag = kSrc * kZoom;
    constexpr int kInfoH = 40;
    // 黑框与上方放大框等宽同左缘，天然居中对齐（中心线重合）
    constexpr int kPanelW = kMag;

    // 当前像素颜色（从截图小区域取，避免整屏转 QImage）
    QColor color(Qt::black);
    const QPixmap one = m_screenshot.copy(QRect(m_mousePos, QSize(1, 1)));
    if (!one.isNull())
        color = one.toImage().pixelColor(0, 0);

    // 四象限择优：默认左下；放不下依次换位。拖拽时优先选区外侧，避免盖住正在画的选区
    // 取色框角点不压准星中心，而是落在准星圆周的对角点（45°/135°/225°/315°）上：
    // 准星圆半径约 8px，对角方向分量 = 8/√2 ≈ 6px。面板朝哪个象限展开，
    // 朝向鼠标的角点就偏移到该象限的圆周对角点，如左下展开→角点在圆周左下(225°)处
    constexpr int kDiag = 6;
    const int kPanelH = kMag + kInfoH;
    const int westX = m_mousePos.x() - kDiag - kPanelW;
    const int eastX = m_mousePos.x() + kDiag;
    const int southY = m_mousePos.y() + kDiag;
    const int northY = m_mousePos.y() - kDiag - kPanelH;

    bool prefEast = false;
    bool prefNorth = false;
    if (m_dragging) {
        // 鼠标是选区活动角：面板放到活动角外侧
        prefEast = m_mousePos.x() >= m_origin.x();
        prefNorth = m_mousePos.y() < m_origin.y();
    }
    const int prefX = prefEast ? eastX : westX;
    const int altX = prefEast ? westX : eastX;
    const int prefY = prefNorth ? northY : southY;
    const int altY = prefNorth ? southY : northY;
    const QPoint candidates[4] = {{prefX, prefY}, {prefX, altY}, {altX, prefY}, {altX, altY}};

    int x = candidates[0].x();
    int y = candidates[0].y();
    bool placed = false;
    for (const QPoint& c : candidates) {
        const QRect r(c.x(), c.y(), kPanelW, kPanelH);
        if (r.left() >= 0 && r.top() >= 0 && r.right() < width() && r.bottom() < height()) {
            x = c.x();
            y = c.y();
            placed = true;
            break;
        }
    }
    if (!placed) {
        // 极小屏幕等无象限可放时，夹回屏幕内
        x = qBound(0, x, qMax(0, width() - kPanelW));
        y = qBound(0, y, qMax(0, height() - kPanelH));
    }

    const QRect magRect(x + (kPanelW - kMag) / 2, y, kMag, kMag);
    const QRect infoRect(x, y + kMag, kPanelW, kInfoH);

    // 放大图（最近邻，保持像素颗粒）
    QRect src(m_mousePos.x() - kSrc / 2, m_mousePos.y() - kSrc / 2, kSrc, kSrc);
    src = src.intersected(rect());
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.fillRect(magRect, Qt::black);
    if (src.isValid())
        p.drawPixmap(magRect, m_screenshot, src);
    // 像素网格
    p.setPen(QPen(QColor(255, 255, 255, 28), 1));
    for (int i = 1; i < kSrc; ++i) {
        p.drawLine(magRect.left() + i * kZoom,
                   magRect.top(),
                   magRect.left() + i * kZoom,
                   magRect.bottom());
        p.drawLine(
            magRect.left(), magRect.top() + i * kZoom, magRect.right(), magRect.top() + i * kZoom);
    }
    // 蓝色十字 + 中心像素白框
    const int cx = magRect.left() + (kSrc / 2) * kZoom;
    const int cy = magRect.top() + (kSrc / 2) * kZoom;
    p.setPen(QPen(QColor(40, 90, 255), 1));
    p.drawLine(magRect.left(), cy + kZoom / 2, magRect.right(), cy + kZoom / 2);
    p.drawLine(cx + kZoom / 2, magRect.top(), cx + kZoom / 2, magRect.bottom());
    p.setPen(QPen(Qt::white, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(cx, cy, kZoom - 1, kZoom - 1);
    p.setPen(QPen(QColor(30, 30, 30), 1));
    p.drawRect(magRect.adjusted(0, 0, -1, -1));
    p.restore();

    // 信息条：屏幕坐标 + RGB；与放大框等宽，文字水平居中
    const QPoint global = mapToGlobal(m_mousePos);
    p.fillRect(infoRect, QColor(15, 15, 15, 225));
    p.setPen(Qt::white);
    p.setFont(QFont(QStringLiteral("Consolas"), 9));
    const QString line1 = QStringLiteral("(%1, %2)").arg(global.x()).arg(global.y());
    const QString line2 = QStringLiteral("RGB #%1%2%3")
                              .arg(color.red(), 2, 16, QLatin1Char('0'))
                              .arg(color.green(), 2, 16, QLatin1Char('0'))
                              .arg(color.blue(), 2, 16, QLatin1Char('0'));
    p.drawText(infoRect, Qt::AlignCenter, line1 + QLatin1Char('\n') + line2);
}

void CaptureOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    // m_screenshot 已设置 devicePixelRatio，按逻辑坐标铺满，输出仍为物理像素
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
                ResizeHandle::TopLeft,
                ResizeHandle::Top,
                ResizeHandle::TopRight,
                ResizeHandle::Right,
                ResizeHandle::BottomRight,
                ResizeHandle::Bottom,
                ResizeHandle::BottomLeft,
                ResizeHandle::Left,
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

        for (const Annotation& a : m_annotations)
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
        auto& i18n = I18n::instance();
        p.setPen(QColor(255, 255, 255, 200));
        p.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 12));
        p.drawText(QPoint(24, 36), i18n.tr(QStringLiteral("capture_tip")));
    }

    // 画选区之前 / 画选区时显示；选区确定进入编辑后消失
    if (!m_editing)
        drawMagnifierInfo(p);
}

void CaptureOverlay::mousePressEvent(QMouseEvent* event) {
    m_mousePos = event->pos();
    m_hasMousePos = true;
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
        // 选了标注工具时，按在已画图形边框上优先拖动该图形（而不是新画一个），
        // 并把该图形对应的工具激活（高亮），其它工具自动取消激活
        if (m_tool != Tool::None) {
            const int ann = hitTestAnnotation(event->pos());
            if (ann >= 0) {
                commitTextEntry();
                m_movingAnnIndex = ann;
                m_hoverAnnIndex = ann;
                m_annDragLast = event->pos();
                setTool(m_annotations.at(ann).type);
                updateCursor();
                return;
            }
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
        // 开始画线即切到系统箭头（与选工具后的要求一致）
        updateCursor();
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
    setCursor(crosshairCursor());
    update();
}

void CaptureOverlay::mouseMoveEvent(QMouseEvent* event) {
    m_mousePos = event->pos();
    m_hasMousePos = true;
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
        if (m_movingAnnIndex >= 0) {
            // 拖动已画图形：整体平移，夹在选区内不拖出界
            Annotation& a = m_annotations[m_movingAnnIndex];
            const QRect bounds = annotationBounds(a);
            const QRect sel = selectionRect();
            QPoint delta = event->pos() - m_annDragLast;
            delta.setX(qBound(sel.left() - bounds.left(), delta.x(), sel.right() - bounds.right()));
            delta.setY(qBound(sel.top() - bounds.top(), delta.y(), sel.bottom() - bounds.bottom()));
            if (!delta.isNull()) {
                a.p1 += delta;
                a.p2 += delta;
                for (QPoint& pt : a.path)
                    pt += delta;
                m_annDragLast += delta;
            }
            updateCursor();
            update();
            return;
        }
        if (m_drawing) {
            m_drawCurrent = clampToSelection(event->pos());
            if (m_tool == Tool::Pen)
                m_penPath.push_back(m_drawCurrent);
            // 拖拽画线全程保持系统箭头
            updateCursor();
            update();
            return;
        }
        const ResizeHandle hover = hitTestHandle(event->pos());
        const bool inSel = selectionRect().contains(event->pos());
        m_hoverHandle = hover;
        m_hoverAnnIndex = (m_tool != Tool::None) ? hitTestAnnotation(event->pos()) : -1;
        m_pointerInSelection = inSel;
        // 每帧按当前工具/位置刷新，避免状态未变化时光标停留在旧样式
        updateCursor();
        return;
    }

    m_current = event->pos();
    if (m_fixedMode) {
        m_hasSelection = true;
        update();
        return;
    }
    // 未拖拽时也要跟随刷新放大镜
    update();
}

void CaptureOverlay::mouseReleaseEvent(QMouseEvent* event) {
    m_mousePos = event->pos();
    m_hasMousePos = true;
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

    if (m_editing && m_movingAnnIndex >= 0) {
        m_movingAnnIndex = -1;
        m_hoverAnnIndex = (m_tool != Tool::None) ? hitTestAnnotation(event->pos()) : -1;
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

void CaptureOverlay::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        if (m_textEntry && m_textEntry->isVisible()) {
            m_textEntry->hide();
            return;
        }
        emit cancelled();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && m_editing &&
        !(m_textEntry && m_textEntry->isVisible())) {
        confirm(false);
        return;
    }
    if (event->matches(QKeySequence::Undo) && m_editing) {
        undoLast();
        return;
    }
}
