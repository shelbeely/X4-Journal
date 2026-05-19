#pragma once

/* Compiled only when CONFIG_X4_DIAG_HTTP_API is enabled. */
#ifdef CONFIG_X4_DIAG_HTTP_API

#include <WebServer.h>

#ifdef __cplusplus
extern "C++" {
#endif

/* Register /api/dev/* routes.
   Requires Authorization: Bearer <token> header when CONFIG_X4_DIAG_API_TOKEN
   is set (non-empty).  No auth when the token is empty. */
void api_dev_register(WebServer *server);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_X4_DIAG_HTTP_API */
