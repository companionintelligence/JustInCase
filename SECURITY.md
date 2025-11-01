# Security Notes

This document outlines security measures implemented and known limitations.

## Security Fixes Implemented

### Critical (P0) Fixes
1. **Request Size Limits**: Maximum 10MB request size prevents memory exhaustion attacks
2. **Socket Timeouts**: 30-second timeout prevents slowloris-style DoS attacks
3. **Input Validation**: Content-Length header is validated before use
4. **Exception Handling**: All parsing operations wrapped in try-catch blocks
5. **Rate Limiting**: 60 requests per minute per IP address

### High Priority (P1) Fixes
1. **Path Traversal Protection**: Uses `std::filesystem::canonical()` to prevent directory traversal
2. **Security Headers**: Implements multiple security headers (see below)
3. **Connection Limits**: Increased backlog to handle more concurrent connections
4. **Credential Warnings**: Warns when default database credentials are in use

## Security Headers

The following security headers are automatically added to all responses:

- **X-Frame-Options: DENY** - Prevents clickjacking attacks
- **X-Content-Type-Options: nosniff** - Prevents MIME sniffing
- **X-XSS-Protection: 1; mode=block** - Enables XSS protection
- **Content-Security-Policy** - Restricts resource loading
- **Referrer-Policy: strict-origin-when-cross-origin** - Controls referrer information

## Known Limitations

### Content Security Policy (CSP)
The current CSP allows `'unsafe-inline'` for both scripts and styles:
```
Content-Security-Policy: default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'
```

**Reason**: The existing frontend code uses inline scripts and styles. Removing `'unsafe-inline'` would require refactoring all frontend code to use external files or CSP nonces/hashes.

**Recommendation**: For production deployments, consider refactoring frontend code to eliminate inline scripts and update CSP to remove `'unsafe-inline'`.

### CORS Configuration
CORS is currently set to allow all origins (`*`):
```
Access-Control-Allow-Origin: *
```

**Reason**: This maintains existing application behavior and supports the current use case where the application may be accessed from various origins.

**Recommendation**: For production deployments, restrict CORS to specific trusted domains:
```cpp
response << "Access-Control-Allow-Origin: https://yourdomain.com\r\n";
```

## Environment Variables

For production deployments, always set the following environment variables:

- `POSTGRES_USER` - Database username (do not use default)
- `POSTGRES_PASSWORD` - Database password (do not use default)

The application will warn on startup if default credentials are being used.

## Rate Limiting

Rate limiting is implemented per IP address:
- Window: 60 seconds
- Limit: 60 requests per window
- Response: HTTP 429 Too Many Requests

## Requirements

- **C++ Standard**: C++17 or later (required for `std::filesystem` and structured bindings)
- **Compiler**: GCC 8+, Clang 7+, or MSVC 2017+

## Future Improvements

Consider implementing these additional security measures:

1. **TLS/HTTPS Support**: Currently the server uses HTTP. Add TLS support for encrypted communications.
2. **Authentication**: Implement proper authentication mechanism for sensitive endpoints.
3. **Request Signing**: Add HMAC-based request signing for API endpoints.
4. **Audit Logging**: Log all security-relevant events (rate limit violations, path traversal attempts, etc.)
5. **Input Sanitization**: Add comprehensive input sanitization for all user-provided data.
