#pragma once

#include <QHash>
#include <QLineEdit>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <QWidget>

class QPushButton;
class QToolButton;

/** 全屏框选 + 标注工具栏（矩形/椭圆/箭头/画笔/文字/撤销/取消/复制/AI问询）。 */
class CaptureOverlay : public QWidget {
    Q_OBJECT
public:
    explicit CaptureOverlay(const QPixmap& screenshot,
                            bool fixedMode,
                            int fixedW,
                            int fixedH,
                            QWidget* parent = nullptr);

signals:
    void finished(const QPixmap& image, bool sendAi);
    void cancelled();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    enum class Tool { None, Rect, Ellipse, Arrow, Pen, Text };

    enum class ResizeHandle {
        None,
        TopLeft,
        Top,
        TopRight,
        Right,
        BottomRight,
        Bottom,
        BottomLeft,
        Left
    };

    struct Annotation {
        Tool type = Tool::None;
        QPoint p1;
        QPoint p2;
        QVector<QPoint> path;
        QString text;
    };

    QRect selectionRect() const;
    QPoint clampToSelection(const QPoint& p) const;
    QPixmap renderResult() const;
    void showToolbar();
    void hideToolbar();
    void confirm(bool sendAi);
    void setTool(Tool tool);
    void onToolClicked(const QString& key);
    void undoLast();
    void commitTextEntry();
    void placeTextEntry(const QPoint& pos);
    void drawAnnotation(QPainter& p, const Annotation& a, const QPoint& origin = {}) const;
    void drawArrow(QPainter& p, const QPoint& from, const QPoint& to) const;
    /** 命中已画标注的边框（矩形/椭圆/箭头/文字），返回下标（上层优先），无命中 -1 */
    int hitTestAnnotation(const QPoint& pos) const;
    /** 标注的外接框（拖动时用于把移动范围夹在选区内） */
    QRect annotationBounds(const Annotation& a) const;
    /** 选区确定前：鼠标左下角的放大镜 + 坐标/像素颜色（选区确定后不绘制） */
    void drawMagnifierInfo(QPainter& p) const;
    void updateToolHighlights();
    void updateCursor();
    ResizeHandle hitTestHandle(const QPoint& pos) const;
    QPoint handleCenter(ResizeHandle handle) const;
    QRect resizedSelection(ResizeHandle handle, const QPoint& pos) const;
    QRect movedSelection(const QPoint& delta) const;
    void offsetAnnotations(const QPoint& delta);
    static Qt::CursorShape cursorForHandle(ResizeHandle handle);
    static QIcon loadToolIcon(const QString& key, bool active);

    QPixmap m_screenshot;
    bool m_fixedMode = false;
    int m_fixedW = 800;
    int m_fixedH = 600;

    bool m_dragging = false;
    bool m_selecting = true;
    bool m_editing = false;
    QPoint m_origin;
    QPoint m_current;
    bool m_hasSelection = false;
    QRect m_lockedSelection; // 进入标注后固定选区，避免鼠标移动改变框大小
    bool m_resizing = false;
    bool m_moving = false;
    ResizeHandle m_activeHandle = ResizeHandle::None;
    ResizeHandle m_hoverHandle = ResizeHandle::None;
    QPoint m_moveOrigin;
    QRect m_moveStartSel;
    bool m_pointerInSelection = false;
    QPoint m_mousePos;
    bool m_hasMousePos = false;

    Tool m_tool = Tool::None;
    bool m_drawing = false;
    QPoint m_drawStart;
    QPoint m_drawCurrent;
    QVector<QPoint> m_penPath;
    QVector<Annotation> m_annotations;
    int m_hoverAnnIndex = -1;  // 悬停命中的标注（边框上显示可拖拽光标）
    int m_movingAnnIndex = -1; // 正在拖动移动的标注
    QPoint m_annDragLast;      // 拖动标注时上一帧鼠标位置

    QWidget* m_toolbar = nullptr;
    QHash<QString, QToolButton*> m_toolButtons;
    QLineEdit* m_textEntry = nullptr;
    QPoint m_textPos;
};
