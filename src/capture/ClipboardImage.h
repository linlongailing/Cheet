#pragma once

#include <QPixmap>
#include <QString>

namespace ClipboardImage {

/**
 * 写入系统剪贴板，便于粘贴到文档/网页，以及资源管理器（桌面/文件夹）。
 * - 位图 / PNG：Word、浏览器等
 * - 临时 PNG 文件 + 文件列表：桌面「粘贴」生成图片文件
 * 返回临时 PNG 路径（可能为空表示失败）。
 */
QString copyForPaste(const QPixmap &image);

} // namespace ClipboardImage
