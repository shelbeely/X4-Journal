#pragma once

#ifdef __cplusplus
#include <WebServer.h>

void api_entries_register(WebServer &server);

/* Called by web_server.cpp to dispatch /api/entries/:id requests */
void api_entries_handle_single(WebServer &server);
#endif
