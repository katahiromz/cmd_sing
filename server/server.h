// server.h --- cmd_sing_server.exe のヘッダ
// Copyright (C) 2025 Katayama Hirofumi MZ (katayama.hirofumi.mz@gmail.com)

#pragma once

#define SERVER_CLASSNAME L"cmd_sing server"
#define SERVER_TITLE L"cmd_sing server"

inline HWND
find_server_window(VOID)
{
    return FindWindowW(SERVER_CLASSNAME, SERVER_TITLE);
}
