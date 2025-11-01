# Security Deployment Checklist

This checklist must be completed before deploying JustInCase to production.

---

## Pre-Deployment Security Checklist

### 1. Configuration & Secrets ✓/✗
- [ ] All hardcoded credentials removed from code
- [ ] Environment variables used for all sensitive config
- [ ] Strong database password set (min 20 characters, random)
- [ ] API key configured (if authentication enabled)
- [ ] Database connection limited to localhost or private network
- [ ] Model files stored securely with proper permissions
- [ ] No secrets in version control or docker images
- [ ] `.env` file not committed to repository
- [ ] Sensitive files added to `.gitignore`

### 2. Input Validation ✓/✗
- [ ] All HTTP request parsing has size limits
- [ ] JSON input validation implemented
- [ ] Query length limits enforced
- [ ] Path traversal protection verified
- [ ] Content-Length header validation
- [ ] URL decoding implemented for file paths
- [ ] Special characters sanitized
- [ ] File upload limits configured (if applicable)

### 3. Authentication & Authorization ✓/✗
- [ ] Authentication enabled for production (`REQUIRE_AUTH=true`)
- [ ] API keys generated and distributed securely
- [ ] Rate limiting configured and tested
- [ ] Per-IP rate limits implemented
- [ ] CORS configured for specific origins only
- [ ] No wildcard CORS in production
- [ ] Authorization checks on all protected endpoints
- [ ] Session management reviewed (conversation history)

### 4. Network Security ✓/✗
- [ ] TLS/HTTPS configured on nginx
- [ ] TLS 1.2+ only (no SSL, no TLS 1.0/1.1)
- [ ] Strong cipher suites configured
- [ ] Certificate from trusted CA installed
- [ ] Certificate expiry monitoring setup
- [ ] HSTS header configured
- [ ] nginx reverse proxy configured correctly
- [ ] Backend server not exposed directly to internet
- [ ] Firewall rules configured (only 443, 80 open)
- [ ] Internal services (Tika, DB) not externally accessible

### 5. HTTP Security Headers ✓/✗
- [ ] `Strict-Transport-Security` header set
- [ ] `X-Frame-Options: DENY` header set
- [ ] `X-Content-Type-Options: nosniff` header set
- [ ] `X-XSS-Protection` header set
- [ ] `Content-Security-Policy` configured
- [ ] `Referrer-Policy` set appropriately
- [ ] Server version information hidden
- [ ] Unnecessary headers removed

### 6. Resource Limits ✓/✗
- [ ] Request size limits configured (10MB max)
- [ ] Connection limits set (100 concurrent)
- [ ] Memory limits configured for containers
- [ ] CPU limits configured for containers
- [ ] Disk space monitoring setup
- [ ] Socket timeouts configured
- [ ] HTTP request timeout configured
- [ ] LLM generation timeout set
- [ ] Conversation history size limited

### 7. Error Handling & Logging ✓/✗
- [ ] Error messages sanitized (no sensitive info exposed)
- [ ] Stack traces disabled in production
- [ ] Proper HTTP status codes used
- [ ] All errors logged appropriately
- [ ] Security events logged (auth failures, rate limits)
- [ ] Log rotation configured
- [ ] Logs stored securely with proper permissions
- [ ] Log monitoring and alerting setup
- [ ] Request logging enabled
- [ ] Anomaly detection configured

### 8. Code Security ✓/✗
- [ ] Buffer overflow protections implemented
- [ ] No race conditions in critical sections
- [ ] Memory leaks tested and fixed
- [ ] Static analysis run (CodeQL, Coverity)
- [ ] Dependencies scanned for vulnerabilities
- [ ] Compiler security flags enabled (`-fstack-protector`, etc.)
- [ ] ASLR enabled on host system
- [ ] Non-root user for application process
- [ ] Minimal container image (remove build tools)

### 9. Testing ✓/✗
- [ ] Unit tests passing (80%+ coverage)
- [ ] Integration tests passing
- [ ] Security tests passing
- [ ] Fuzz testing completed
- [ ] Load testing completed (1000+ concurrent)
- [ ] Penetration testing completed
- [ ] Path traversal tests passed
- [ ] Authentication bypass tests passed
- [ ] Rate limiting tests passed
- [ ] Memory leak tests passed (24hr run)

### 10. Infrastructure ✓/✗
- [ ] OS security updates applied
- [ ] Docker security best practices followed
- [ ] Container running as non-root user
- [ ] Host firewall configured (ufw/iptables)
- [ ] Intrusion detection system configured (optional)
- [ ] File integrity monitoring enabled (optional)
- [ ] Backup system configured
- [ ] Disaster recovery plan documented
- [ ] High availability setup reviewed (if applicable)

### 11. Monitoring & Alerting ✓/✗
- [ ] Health check endpoint configured
- [ ] Uptime monitoring setup
- [ ] Performance monitoring configured
- [ ] Error rate alerting setup
- [ ] Security event alerting configured
- [ ] Resource usage monitoring (CPU, memory, disk)
- [ ] Log aggregation configured
- [ ] Metrics dashboard created
- [ ] On-call rotation defined
- [ ] Incident response plan documented

### 12. Documentation ✓/✗
- [ ] Security architecture documented
- [ ] Deployment guide updated
- [ ] Configuration guide completed
- [ ] Security best practices documented
- [ ] Incident response procedures documented
- [ ] Backup and recovery procedures documented
- [ ] Monitoring and alerting guide completed
- [ ] Contact information for security issues documented
- [ ] SECURITY.md file created
- [ ] README updated with security information

### 13. Compliance & Legal ✓/✗
- [ ] Privacy policy created (if collecting user data)
- [ ] Terms of service created
- [ ] Data retention policy defined
- [ ] GDPR compliance reviewed (if applicable)
- [ ] CCPA compliance reviewed (if applicable)
- [ ] Data processing agreements signed
- [ ] Security audit report filed
- [ ] Insurance coverage reviewed

### 14. Third-Party Services ✓/✗
- [ ] Tika service secured (not publicly accessible)
- [ ] Database secured (strong password, limited access)
- [ ] Communication between services encrypted
- [ ] Service-to-service authentication implemented
- [ ] Third-party dependencies up to date
- [ ] Dependency vulnerability scanning automated
- [ ] Supply chain security reviewed

### 15. Deployment Process ✓/✗
- [ ] Deployment checklist created
- [ ] Rollback procedure documented
- [ ] Blue-green deployment configured (if applicable)
- [ ] Canary deployment process defined (if applicable)
- [ ] Post-deployment verification tests defined
- [ ] Smoke tests automated
- [ ] Database migration tested
- [ ] Configuration management automated
- [ ] Secrets management system in place

---

## Post-Deployment Verification

### Immediate (Within 1 hour)
- [ ] Service is responding (200 OK on health check)
- [ ] HTTPS working correctly
- [ ] Security headers present (verify with curl)
- [ ] Rate limiting working
- [ ] Authentication working (if enabled)
- [ ] Logs being generated
- [ ] No errors in application logs
- [ ] No errors in nginx logs
- [ ] Certificate valid and trusted
- [ ] All endpoints responding correctly

### Short-term (Within 24 hours)
- [ ] No memory leaks detected
- [ ] No performance degradation
- [ ] Error rate < 1%
- [ ] Response time < 2 seconds (p95)
- [ ] All alerts configured and working
- [ ] Monitoring dashboard accessible
- [ ] Backup completed successfully
- [ ] Log rotation working
- [ ] Security scan passed (external)

### Medium-term (Within 1 week)
- [ ] Load testing in production environment
- [ ] Security monitoring reviewed
- [ ] Performance metrics analyzed
- [ ] User feedback collected
- [ ] Incident response tested
- [ ] Backup restoration tested
- [ ] Documentation reviewed by team
- [ ] Training completed for operations team

---

## Security Testing Commands

### Test HTTPS Configuration
```bash
# Check SSL/TLS configuration
openssl s_client -connect your-domain.com:443 -servername your-domain.com

# Check certificate expiry
openssl s_client -connect your-domain.com:443 -servername your-domain.com 2>/dev/null | openssl x509 -noout -dates

# SSL Labs scan
# Visit: https://www.ssllabs.com/ssltest/analyze.html?d=your-domain.com
```

### Test Security Headers
```bash
# Check all headers
curl -I https://your-domain.com

# Check specific security headers
curl -s -I https://your-domain.com | grep -i "strict-transport-security\|x-frame-options\|x-content-type\|x-xss-protection\|content-security-policy"
```

### Test Rate Limiting
```bash
# Send multiple requests quickly
for i in {1..100}; do
  curl -X POST https://your-domain.com/query \
    -H "Content-Type: application/json" \
    -H "Authorization: Bearer YOUR_API_KEY" \
    -d '{"query":"test"}' &
done
wait
```

### Test Authentication
```bash
# Without API key (should fail)
curl -X POST https://your-domain.com/query \
  -H "Content-Type: application/json" \
  -d '{"query":"test"}'

# With valid API key (should succeed)
curl -X POST https://your-domain.com/query \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer YOUR_API_KEY" \
  -d '{"query":"test"}'
```

### Test Path Traversal
```bash
# Should all return 403 or 404
curl https://your-domain.com/../etc/passwd
curl https://your-domain.com/%2e%2e%2fetc%2fpasswd
curl https://your-domain.com/..%2fetc%2fpasswd
```

### Test Input Validation
```bash
# Large request (should fail with 413)
dd if=/dev/zero bs=1M count=11 | curl -X POST https://your-domain.com/query \
  -H "Content-Type: application/json" \
  --data-binary @-

# Malformed JSON (should fail with 400)
curl -X POST https://your-domain.com/query \
  -H "Content-Type: application/json" \
  -d '{invalid json}'
```

---

## Emergency Procedures

### Security Incident Response
1. **Detect:** Monitor alerts, logs, and user reports
2. **Assess:** Determine severity and impact
3. **Contain:** Isolate affected systems, block attackers
4. **Eradicate:** Remove malicious code, fix vulnerabilities
5. **Recover:** Restore from backups, verify integrity
6. **Learn:** Post-mortem, update procedures

### Emergency Contacts
- **Security Team Lead:** [Name/Email/Phone]
- **Infrastructure Team:** [Name/Email/Phone]
- **On-call Engineer:** [Name/Email/Phone]
- **Management Escalation:** [Name/Email/Phone]

### Quick Actions
- **Under DDoS Attack:** Enable Cloudflare, increase rate limits temporarily
- **Compromised Credentials:** Rotate immediately, revoke old credentials
- **Zero-day Vulnerability:** Take offline if critical, patch immediately
- **Data Breach:** Follow incident response plan, notify stakeholders

---

## Maintenance Schedule

### Daily
- [ ] Review security logs
- [ ] Check error rates
- [ ] Monitor resource usage
- [ ] Review alerts

### Weekly
- [ ] Security scan
- [ ] Backup verification
- [ ] Certificate expiry check
- [ ] Performance review

### Monthly
- [ ] Security updates
- [ ] Dependency updates
- [ ] Penetration test (light)
- [ ] Access review
- [ ] Log analysis

### Quarterly
- [ ] Full security audit
- [ ] Penetration test (comprehensive)
- [ ] Disaster recovery drill
- [ ] Security training
- [ ] Policy review

### Annually
- [ ] External security audit
- [ ] Compliance audit
- [ ] Insurance review
- [ ] Major version updates

---

## Sign-off

| Role | Name | Date | Signature |
|------|------|------|-----------|
| Developer | | | |
| Security Lead | | | |
| DevOps Lead | | | |
| Project Manager | | | |

**Deployment Authorization:**

I confirm that all critical security items have been reviewed and addressed:

Name: ___________________________  
Role: ___________________________  
Date: ___________________________  
Signature: _______________________

---

**Document Version:** 1.0  
**Last Updated:** November 1, 2025  
**Review Frequency:** Before each production deployment
