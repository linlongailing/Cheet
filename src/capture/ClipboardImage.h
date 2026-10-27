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
QString copyForPaste(const QPixmap& image);

/**
 * 浏览器网页粘贴专用：只放 PNG / DIB / image/png，不带 CF_HDROP。
 * 豆包等站点在同时有文件列表时可能不走上图逻辑。
 */
QString copyForWebPaste(const QPixmap& image);

/**
 * 暂停/恢复剪贴板守护（仅 GUI 线程调用）。
 * 守护：截图写入后若剪贴板被清空则自动放回，直到用户复制别的内容为止。
 * AI 发送流程要在剪贴板上来回贴图贴文，期间须暂停。
 */
void setGuardSuspended(bool suspended);

} // namespace ClipboardImage
