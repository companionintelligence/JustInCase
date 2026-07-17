// ── JIC web UI ───────────────────────────────────────────────────────
// Vanilla JS, no dependencies, CSP-safe (no inline handlers/styles).
// Talks to: POST /query · GET /status · GET /api/library

'use strict';

(() => {
  const $ = (id) => document.getElementById(id);

  const state = {
    conversationId: newConversationId(),
    useContext: true,
    busy: false,
    lastFileCount: -1,
    statusFailures: 0,
  };

  const CATEGORY_LABELS = {
    '100_Survival':    'Survival & preparedness',
    '200_Medical':     'Medical',
    '300_Food':        'Food & agriculture',
    '400_Engineering': 'Engineering & water',
    '500_Comms':       'Communications',
    '600_Education':   'Education',
    '700_Social':      'Civic & social',
    '800_Software':    'Software & technical',
  };

  const SUGGESTED_PROMPTS = [
    'How do I purify water in the field?',
    'First aid for a severe burn',
    'Build a 72-hour emergency kit',
    'How do I treat a wound to prevent infection?',
    'Wire a solar panel to a battery',
    'Preserve food without refrigeration',
  ];

  // ── Helpers ────────────────────────────────────────────────────────

  function newConversationId() {
    if (window.crypto && crypto.randomUUID) return 'conv-' + crypto.randomUUID();
    return 'conv-' + Date.now() + '-' + Math.random().toString(36).slice(2, 10);
  }

  function escapeHtml(text) {
    return String(text)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }

  // Minimal markdown → HTML (input is escaped first, so injection-safe)
  function renderMarkdown(text) {
    let html = escapeHtml(text)
      .replace(/```(\w*)\n([\s\S]*?)```/g, '<pre><code>$2</code></pre>')
      .replace(/`([^`]+)`/g, '<code>$1</code>')
      .replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>')
      .replace(/\*(.+?)\*/g, '<em>$1</em>')
      .replace(/^### (.+)$/gm, '<h4>$1</h4>')
      .replace(/^## (.+)$/gm, '<h3>$1</h3>')
      .replace(/^# (.+)$/gm, '<h2>$1</h2>')
      .replace(/^[-*] (.+)$/gm, '<li>$1</li>')
      .replace(/^\d+\. (.+)$/gm, '<li>$1</li>')
      .replace(/((?:<li>.*<\/li>\n?)+)/g, '<ul>$1</ul>')
      .replace(/\n\n+/g, '</p><p>')
      .replace(/\n/g, '<br>');
    return '<p>' + html + '</p>';
  }

  function formatUptime(seconds) {
    if (seconds < 90) return seconds + 's';
    if (seconds < 5400) return Math.round(seconds / 60) + 'm';
    if (seconds < 129600) return Math.round(seconds / 3600) + 'h';
    return Math.round(seconds / 86400) + 'd';
  }

  function formatSize(bytes) {
    if (!bytes) return '';
    if (bytes < 1048576) return Math.max(1, Math.round(bytes / 1024)) + ' KB';
    return (bytes / 1048576).toFixed(1) + ' MB';
  }

  // ── Chat rendering ─────────────────────────────────────────────────

  function addMessage(role, content, opts = {}) {
    const chat = $('chat');
    const msg = document.createElement('div');
    msg.className = 'msg ' + role + (opts.error ? ' error' : '');

    const meta = document.createElement('div');
    meta.className = 'msg-meta';
    meta.textContent = role === 'user' ? 'You' : 'JIC';

    const bubble = document.createElement('div');
    bubble.className = 'msg-bubble';
    if (role === 'bot') bubble.innerHTML = renderMarkdown(content);
    else bubble.textContent = content;

    msg.append(meta, bubble);
    chat.appendChild(msg);
    scrollChat();
    return msg;
  }

  function addSources(matches) {
    if (!matches || !matches.length) return;
    const chat = $('chat');
    const details = document.createElement('details');
    details.className = 'sources';

    const summary = document.createElement('summary');
    summary.textContent = `Sources · ${matches.length} document${matches.length > 1 ? 's' : ''}`;
    details.appendChild(summary);

    matches.forEach((m) => {
      const item = document.createElement('div');
      item.className = 'source-item';
      const score = m.score ? `<span class="source-score">match ${(m.score * 100).toFixed(0)}%</span>` : '';
      item.innerHTML =
        `<a href="/sources/${encodeURI(m.filename)}" target="_blank" rel="noopener">${escapeHtml(m.filename)}</a>` +
        score +
        `<p class="source-snippet">${escapeHtml(m.text || '')}</p>`;
      details.appendChild(item);
    });

    chat.appendChild(details);
    scrollChat();
  }

  function addTyping() {
    const chat = $('chat');
    const t = document.createElement('div');
    t.className = 'typing';
    t.id = 'typing';
    t.setAttribute('aria-label', 'Searching the field library…');
    t.innerHTML = '<span></span><span></span><span></span>';
    chat.appendChild(t);
    scrollChat();
  }

  function removeTyping() {
    const t = $('typing');
    if (t) t.remove();
  }

  function scrollChat() {
    const chat = $('chat');
    chat.scrollTop = chat.scrollHeight;
  }

  function showWelcome() {
    const chat = $('chat');
    chat.innerHTML = '';
    const w = document.createElement('div');
    w.className = 'welcome';
    w.innerHTML =
      '<svg class="brand-mark" viewBox="0 0 64 64" aria-hidden="true">' +
      '<g fill="none" stroke-linecap="round">' +
      '<circle cx="32" cy="32" r="21" stroke="currentColor" stroke-width="4"/>' +
      '<path d="M32 3v6M32 55v6M3 32h6M55 32h6" stroke="currentColor" stroke-width="4"/>' +
      '<path class="brand-mark-accent" d="M32 22v20M22 32h20" stroke-width="7"/>' +
      '</g></svg>' +
      '<h1>Just in Case</h1>' +
      '<p>Offline emergency knowledge, grounded in the field library on this device. ' +
      'Ask about first aid, water, shelter, power, communications, or anything in the ' +
      'reference collection — answers cite their sources.</p>';
    chat.appendChild(w);
  }

  function renderChips() {
    const wrap = $('chips');
    wrap.innerHTML = '';
    SUGGESTED_PROMPTS.forEach((p) => {
      const b = document.createElement('button');
      b.type = 'button';
      b.className = 'chip';
      b.textContent = p;
      b.addEventListener('click', () => {
        $('user-input').value = p;
        sendMessage();
      });
      wrap.appendChild(b);
    });
  }

  // ── Status & library ───────────────────────────────────────────────

  function setPill(kind, text) {
    const pill = $('status-pill');
    pill.className = 'status-pill ' + kind;
    pill.querySelector('.pill-text').textContent = text;
  }

  function setBanner(text) {
    const b = $('banner');
    if (text) {
      b.textContent = text;
      b.hidden = false;
    } else {
      b.hidden = true;
    }
  }

  async function refreshStatus() {
    try {
      const res = await fetch('/status');
      if (!res.ok) throw new Error('HTTP ' + res.status);
      const s = await res.json();
      state.statusFailures = 0;

      $('st-engine').textContent = s.llm_loaded ? 'online' : 'degraded';
      $('st-engine').className = s.llm_loaded ? 'ok' : 'err';
      $('st-index').textContent = `${s.documents_indexed} chunks / ${s.files_processed} files`;
      $('st-index').className = s.documents_indexed > 0 ? 'ok' : 'warn';
      $('st-model').textContent = s.llm_model || '—';
      $('st-uptime').textContent = formatUptime(s.uptime_seconds || 0);
      $('st-version').textContent = 'v' + (s.version || '?');

      if (!s.llm_loaded) {
        setPill('err', 'LLM missing');
        const where = s.gguf_dir || 'gguf_models/';
        setBanner('Language model not loaded — drop the GGUF files into ' + where +
                  ' and they load automatically within ~30s (no restart needed). ' +
                  'The library stays browsable; answers are unavailable.');
      } else if (s.documents_indexed === 0) {
        setPill('warn', 'Index empty');
        setBanner('');
      } else {
        setPill('ok', `Ready · ${s.files_processed} docs`);
        setBanner('');
      }

      if (s.files_processed !== state.lastFileCount) {
        state.lastFileCount = s.files_processed;
        refreshLibrary();
      }
    } catch (err) {
      state.statusFailures++;
      setPill('err', 'Server unreachable');
      if (state.statusFailures === 1) console.warn('status check failed:', err.message);
    }
  }

  async function refreshLibrary() {
    try {
      const res = await fetch('/api/library');
      if (!res.ok) return;
      const lib = await res.json();

      $('library-count').textContent = lib.total_files;
      const list = $('library-list');
      list.innerHTML = '';

      if (!lib.files.length) {
        const p = document.createElement('p');
        p.className = 'library-empty';
        p.textContent = 'No documents indexed yet. Run the content fetcher or drop ' +
                        'PDFs into the sources volume — they are indexed automatically.';
        list.appendChild(p);
        return;
      }

      const byCat = {};
      lib.files.forEach((f) => {
        (byCat[f.category] = byCat[f.category] || []).push(f);
      });

      Object.keys(byCat).sort().forEach((cat) => {
        const files = byCat[cat];
        const head = document.createElement('div');
        head.className = 'lib-cat';
        head.innerHTML =
          `<span>${escapeHtml(CATEGORY_LABELS[cat] || cat)}</span>` +
          `<span class="lib-cat-n">${files.length}</span>`;
        list.appendChild(head);

        files.forEach((f) => {
          const a = document.createElement('a');
          a.className = 'lib-file' + (f.status === 'skipped' ? ' skipped' : '');
          a.href = '/sources/' + encodeURI(f.filename);
          a.target = '_blank';
          a.rel = 'noopener';
          a.title = f.filename + (f.size_bytes ? ` · ${formatSize(f.size_bytes)}` : '');
          const base = f.filename.split('/').pop();
          a.innerHTML =
            `<span class="lib-name">${escapeHtml(base)}</span>` +
            `<span class="lib-chunks">${f.status === 'skipped' ? 'skipped' : f.chunks}</span>`;
          list.appendChild(a);
        });
      });
    } catch (err) {
      /* sidebar stays as-is on failure */
    }
  }

  // ── Add-content modal (catalog import + upload) ─────────────────────

  let importPoll = null;

  function openLibraryModal() {
    $('library-modal').hidden = false;
    populateUploadCategories();
    loadCatalog();
  }

  function closeLibraryModal() {
    $('library-modal').hidden = true;
    if (importPoll) { clearInterval(importPoll); importPoll = null; }  // stop background polling
  }

  async function loadCatalog() {
    const list = $('catalog-list');
    list.textContent = 'Loading…';
    try {
      const res = await fetch('/api/catalog');
      if (!res.ok) throw new Error('HTTP ' + res.status);
      const data = await res.json();
      renderCatalog(data);
      if (data.importing) pollImport();   // resume progress polling if an import is already running
    } catch (e) {
      list.textContent = 'Could not load the catalog.';
    }
  }

  function renderCatalog(data) {
    const list = $('catalog-list');
    list.innerHTML = '';
    (data.collections || []).forEach((c) => {
      const done = c.count > 0 && c.installed >= c.count;
      const row = document.createElement('div');
      row.className = 'catalog-row';

      const meta = document.createElement('div');
      meta.className = 'catalog-meta';
      meta.innerHTML =
        `<span class="catalog-name">${escapeHtml(c.name)}</span>` +
        `<span class="catalog-desc">${escapeHtml(c.description)}</span>` +
        `<span class="catalog-count">${c.installed}/${c.count} installed</span>`;

      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'ghost-btn catalog-install' + (done ? ' is-done' : '');
      btn.textContent = done ? 'Installed' : (data.importing ? 'Working…' : 'Install');
      btn.disabled = done || data.importing || !data.import_enabled;
      btn.addEventListener('click', () => importCollection(c.id, btn));

      row.appendChild(meta);
      row.appendChild(btn);
      list.appendChild(row);
    });

    if (!data.import_enabled) {
      const note = document.createElement('p');
      note.className = 'modal-note';
      note.textContent = 'Network import is disabled on this deployment — you can still upload your own files below.';
      list.appendChild(note);
    }
  }

  async function importCollection(id, btn) {
    btn.disabled = true;
    btn.textContent = 'Starting…';
    try {
      const res = await fetch('/api/import', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ collection: id }),
      });
      if (res.status === 202) {
        pollImport();
      } else {
        const d = await res.json().catch(() => ({}));
        btn.textContent = 'Retry';
        btn.disabled = false;
        if (res.status === 409) pollImport();
        console.warn('import:', d.error || res.status);
      }
    } catch (e) {
      btn.textContent = 'Retry';
      btn.disabled = false;
    }
  }

  // While an import runs, refresh the catalog (install progress) and the
  // sidebar library (docs appear as they download + index), until done.
  function pollImport() {
    if (importPoll) return;
    importPoll = setInterval(async () => {
      try {
        const res = await fetch('/api/catalog');
        if (res.ok) {
          const data = await res.json();
          renderCatalog(data);
          refreshLibrary();
          if (!data.importing) { clearInterval(importPoll); importPoll = null; }
        }
      } catch (e) { /* keep polling */ }
    }, 3000);
  }

  function populateUploadCategories() {
    const sel = $('upload-category');
    if (sel.options.length) return;
    Object.keys(CATEGORY_LABELS).forEach((cat) => {
      const o = document.createElement('option');
      o.value = cat;
      o.textContent = CATEGORY_LABELS[cat];
      sel.appendChild(o);
    });
  }

  async function uploadFile(file) {
    const status = $('upload-status');
    status.hidden = false;
    status.className = 'modal-status';
    status.textContent = 'Uploading ' + file.name + '…';
    const fd = new FormData();
    fd.append('category', $('upload-category').value);
    fd.append('file', file);
    try {
      const res = await fetch('/api/upload', { method: 'POST', body: fd });
      const d = await res.json().catch(() => ({}));
      if (res.ok) {
        status.textContent = 'Uploaded ' + (d.filename || file.name) + ' — indexing shortly.';
        resetUploadPicker();
        refreshLibrary();
      } else {
        status.className = 'modal-status is-error';
        status.textContent = 'Upload failed: ' + (d.error || ('HTTP ' + res.status));
      }
    } catch (e) {
      status.className = 'modal-status is-error';
      status.textContent = 'Upload failed.';
    }
  }

  function resetUploadPicker() {
    $('upload-file').value = '';
    $('upload-drop-text').textContent = 'Choose a PDF or TXT file, or drop it here…';
  }

  // ── Query flow ─────────────────────────────────────────────────────

  function setBusy(busy) {
    state.busy = busy;
    $('send-button').disabled = busy;
    $('user-input').disabled = busy;
  }

  async function sendMessage() {
    if (state.busy) return;
    const input = $('user-input');
    const question = input.value.trim();
    if (!question) return;

    $('chips').innerHTML = '';
    const welcome = document.querySelector('.welcome');
    if (welcome) welcome.remove();

    addMessage('user', question);
    input.value = '';
    setBusy(true);
    addTyping();

    try {
      const res = await fetch('/query', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          query: question,
          conversation_id: state.conversationId,
          use_context: state.useContext,
        }),
      });

      const isJson = (res.headers.get('content-type') || '').includes('application/json');
      const data = isJson ? await res.json() : null;

      removeTyping();

      if (!res.ok) {
        const detail = data && data.error ? data.error : `server returned ${res.status}`;
        addMessage('bot', res.status === 503
          ? 'The language model is not loaded on this device yet — ' + detail
          : 'Something went wrong: ' + detail, { error: true });
        return;
      }

      if (data && data.answer && data.answer.trim()) {
        addMessage('bot', data.answer);
      } else {
        addMessage('bot', 'I received an empty response — please try rephrasing the question.', { error: true });
      }
      if (data && data.matches) addSources(data.matches);
    } catch (err) {
      removeTyping();
      addMessage('bot', 'Could not reach the JIC server: ' + err.message, { error: true });
    } finally {
      setBusy(false);
      $('user-input').focus();
    }
  }

  function newChat() {
    state.conversationId = newConversationId();
    showWelcome();
    renderChips();
    $('user-input').focus();
  }

  // ── Theme & sidebar ────────────────────────────────────────────────

  function applyTheme(theme) {
    document.documentElement.setAttribute('data-theme', theme);
    try { localStorage.setItem('jic-theme', theme); } catch (e) { /* private mode */ }
  }

  function initTheme() {
    let saved = null;
    try { saved = localStorage.getItem('jic-theme'); } catch (e) { /* private mode */ }
    applyTheme(saved === 'light' ? 'light' : 'dark');
  }

  function toggleSidebar(open) {
    const sb = $('sidebar');
    const show = open !== undefined ? open : !sb.classList.contains('open');
    sb.classList.toggle('open', show);
    $('backdrop').hidden = !show;
    $('sidebar-toggle').setAttribute('aria-expanded', String(show));
  }

  // ── Init ───────────────────────────────────────────────────────────

  document.addEventListener('DOMContentLoaded', () => {
    initTheme();
    showWelcome();
    renderChips();

    $('composer-form').addEventListener('submit', (e) => {
      e.preventDefault();
      sendMessage();
    });

    $('new-chat-btn').addEventListener('click', newChat);

    $('theme-toggle').addEventListener('click', () => {
      const cur = document.documentElement.getAttribute('data-theme');
      applyTheme(cur === 'light' ? 'dark' : 'light');
    });

    $('context-toggle').addEventListener('change', (e) => {
      state.useContext = e.target.checked;
    });

    $('sidebar-toggle').addEventListener('click', () => toggleSidebar());
    $('backdrop').addEventListener('click', () => toggleSidebar(false));

    // ── Add-content modal wiring ──────────────────────────────────
    $('library-add-btn').addEventListener('click', openLibraryModal);
    $('library-modal-close').addEventListener('click', closeLibraryModal);
    $('library-modal-backdrop').addEventListener('click', closeLibraryModal);
    document.addEventListener('keydown', (e) => {
      if (e.key === 'Escape' && !$('library-modal').hidden) closeLibraryModal();
    });

    const fileInput = $('upload-file');
    const drop = $('upload-drop');
    drop.addEventListener('click', () => fileInput.click());
    fileInput.addEventListener('change', () => {
      if (fileInput.files.length) $('upload-drop-text').textContent = fileInput.files[0].name;
    });
    ['dragover', 'dragenter'].forEach((ev) =>
      drop.addEventListener(ev, (e) => { e.preventDefault(); drop.classList.add('dragover'); }));
    ['dragleave', 'dragend'].forEach((ev) =>
      drop.addEventListener(ev, () => drop.classList.remove('dragover')));
    drop.addEventListener('drop', (e) => {
      e.preventDefault();
      drop.classList.remove('dragover');
      if (e.dataTransfer.files.length) {
        fileInput.files = e.dataTransfer.files;
        $('upload-drop-text').textContent = e.dataTransfer.files[0].name;
      }
    });
    $('upload-form').addEventListener('submit', (e) => {
      e.preventDefault();
      if (fileInput.files.length) {
        uploadFile(fileInput.files[0]);
      } else {
        const s = $('upload-status');
        s.hidden = false;
        s.className = 'modal-status';
        s.textContent = 'Pick a file first.';
      }
    });

    $('user-input').focus();

    refreshStatus();
    refreshLibrary();
    setInterval(refreshStatus, 8000);
  });
})();
