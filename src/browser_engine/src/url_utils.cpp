#include "url_utils.h"
#include <cctype>
#include <cstdlib>
#include <cstring>

static bool is_absolute_url(const char *url) {
  if (!url) return false;
  return strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0;
}

static char *str_dup_n(const char *s, size_t n) {
  char *r = (char *)malloc(n + 1);
  if (r) { memcpy(r, s, n); r[n] = '\0'; }
  return r;
}

char *tactilebrowser_resolve_url(const char *base_url,
                                 const char *candidate_url) {
  if (!candidate_url) return nullptr;
  size_t clen = strlen(candidate_url);

  if (is_absolute_url(candidate_url)) {
    return str_dup_n(candidate_url, clen);
  }

  if (!base_url) {
    if (candidate_url[0] == '/') {
      return str_dup_n(candidate_url, clen);
    }
    char *r = (char *)malloc(clen + 2);
    if (r) { r[0] = '/'; memcpy(r + 1, candidate_url, clen + 1); }
    return r;
  }

  size_t blen = strlen(base_url);
  const char *scheme_end = strstr(base_url, "://");
  if (!scheme_end) return str_dup_n(candidate_url, clen);
  scheme_end += 3;

  const char *path_start = strchr(scheme_end, '/');
  size_t host_len = path_start ? (size_t)(path_start - base_url) : blen;

  if (candidate_url[0] == '/' && candidate_url[1] == '/') {
    char *r = (char *)malloc(clen + 6);
    if (r) { memcpy(r, "http:", 5); memcpy(r + 5, candidate_url, clen + 1); }
    return r;
  }

  if (candidate_url[0] == '/') {
    char *r = (char *)malloc(host_len + clen + 1);
    if (r) { memcpy(r, base_url, host_len); memcpy(r + host_len, candidate_url, clen + 1); }
    return r;
  }

  size_t base_path_len = path_start ? (size_t)(blen - host_len) : 1;
  const char *last_slash = nullptr;
  if (path_start) {
    last_slash = strrchr(base_url + host_len, '/');
  }

  size_t prefix_len;
  if (last_slash) {
    prefix_len = (size_t)(last_slash - base_url) + 1;
  } else {
    prefix_len = host_len;
  }

  char *r = (char *)malloc(prefix_len + clen + 2);
  if (!r) return nullptr;
  memcpy(r, base_url, prefix_len);
  r[prefix_len] = '/';
  memcpy(r + prefix_len + 1, candidate_url, clen + 1);
  return r;
}

char *tactilebrowser_extract_path(const char *absolute_url) {
  if (!absolute_url) return nullptr;

  const char *scheme_end = strstr(absolute_url, "://");
  const char *host_start = scheme_end ? scheme_end + 3 : absolute_url;

  const char *path_start = strchr(host_start, '/');
  if (!path_start) {
    return str_dup_n("/", 1);
  }

  const char *query = strchr(path_start, '?');
  const char *fragment = strchr(path_start, '#');
  const char *end = absolute_url + strlen(absolute_url);

  if (query && query < end) end = query;
  if (fragment && fragment < end) end = fragment;

  return str_dup_n(path_start, (size_t)(end - path_start));
}
