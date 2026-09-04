// pch.h: 这是预编译标头文件。
// 下方列出的文件仅编译一次，提高了将来生成的生成性能。
// 这还将影响 IntelliSense 性能，包括代码完成和许多代码浏览功能。
// 但是，如果此处列出的文件中的任何一个在之间有更新，它们全部都将被重新编译。
// 请勿在此处添加要频繁更新的文件，这将使得性能优势无效。

#ifndef PCH_H
#define PCH_H

// Winsock2 必须在 windows.h(经 framework.h 引入)之前包含，
// 供“联机对战”模块使用（避免 winsock.h / winsock2.h 冲突）。
#include <winsock2.h>
#include <ws2tcpip.h>

// 添加要在此处预编译的标头
// MFC must be included before Windows.h. The lean Windows headers omit the
// legacy Winsock 1 declarations, so Winsock 2 can be included immediately after.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "framework.h"
#include <winsock2.h>
#include <ws2tcpip.h>

#endif //PCH_H
