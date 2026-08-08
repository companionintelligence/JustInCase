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

    html = '<p>' + html + '</p>';

    // The `\n` → `<br>` pass above fires inside the <ul> it just built and
    // right after its closing tag, so a three-bullet answer rendered as
    // <ul><li>…</li><br><li>…</li>…</ul><br> — a stray break between every
    // pair of bullets (invalid as a child of <ul>) plus one more under the
    // list. The blocks also sit inside the wrapping <p>, which the parser
    // force-closes, leaving an empty paragraph that keeps its bottom margin.
    // Together those are the ragged gaps in the answer bubble.
    return html
      // stray breaks introduced around block boundaries
      .replace(/<br>\s*(?=<\/?(?:ul|ol|li|h[2-4]|pre)\b)/g, '')
      .replace(/(<\/(?:ul|ol|h[2-4]|pre)>)\s*<br>/g, '$1')
      // lift blocks out of the paragraph instead of letting the parser do it
      .replace(/(<(?:ul|ol|h[2-4]|pre)\b)/g, '</p>$1')
      .replace(/(<\/(?:ul|ol|h[2-4]|pre)>)/g, '$1<p>')
      .replace(/<p>\s*<\/p>/g, '');
  }

  function formatCount(n) {
    const v = Number(n);
    return Number.isFinite(v) ? v.toLocaleString('en-US') : '—';
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

  // The field library is an inventory of documents, so it should list titles.
  // Printing the raw basename cost it two ways: ".pdf" repeated on 27 of 32
  // rows carried no information and used the width that ran ten names into an
  // ellipsis, and hyphen-joined words read as identifiers rather than as the
  // books they are. The exact filename stays on the row's `title`, and every
  // citation in an answer still prints the full path unchanged — this is the
  // sidebar's label only.
  //
  // A hyphen only becomes a space where it is joining words: before an
  // uppercase letter, or after a lowercase one. That leaves document numbers
  // intact ("FM3-25-26-Map-Reading" → "FM3-25-26 Map Reading", not
  // "FM3 25 26 Map Reading") and never touches CamelCase, so "OpenStax",
  // "ThinkOS" and "Eloquent-JavaScript" survive.
  function displayTitle(filename) {
    const base = String(filename).split('/').pop();
    return base
      .replace(/\.(pdf|txt|md|epub|html?)$/i, '')
      .replace(/[_-](?=[A-Z])/g, ' ')
      .replace(/(?<=[a-z])[_-]/g, ' ')
      .trim();
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
      // A ZIM citation names an ENCYCLOPEDIA ARTICLE, not a file in the
      // sources volume, so it is deliberately not a link: /sources/<title>
      // would 404, and a citation that 404s is worse than one that does not
      // offer to open. It is labelled instead, so the reader can still see
      // which claims came from the field manuals and which from the library.
      const isZim = m.origin === 'zim';
      const label = isZim
        ? `<span class="source-name">${escapeHtml(m.filename)}</span>` +
          `<span class="source-origin">encyclopedia</span>`
        : `<a href="/sources/${encodeURI(m.filename)}" target="_blank" rel="noopener">${escapeHtml(m.filename)}</a>`;
      item.innerHTML =
        label +
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
    SUGGESTED_PROMPTS.forEach((p, i) => {
      const b = document.createElement('button');
      b.type = 'button';
      // At 360px every chip is its own row; six of them took two thirds of the
      // viewport and pushed the welcome copy under the composer, cutting the
      // last line in half. CSS drops the overflow below 620px.
      b.className = 'chip' + (i >= 3 ? ' chip-extra' : '');
      b.textContent = p;
      b.addEventListener('click', () => {
        $('user-input').value = p;
        sendMessage();
      });
      wrap.appendChild(b);
    });
  }

  // ── Status & library ───────────────────────────────────────────────

  // `detail` is the secondary half of the pill ("· 32 docs"). It is dropped
  // below 620px, where the full string pushed "New chat" onto a second line.
  function setPill(kind, text, detail) {
    const pill = $('status-pill');
    pill.className = 'status-pill ' + kind;
    pill.querySelector('.pill-text').textContent = text;
    $('pill-detail').textContent = detail ? ' · ' + detail : '';
  }

  // `parts` are plain strings, except that a { path } object is rendered as a
  // <code> span. The degraded-mode banner names the directory to drop the GGUF
  // files into — the one actionable thing a self-hoster needs from it — and set
  // in running prose an absolute container path reads as a leaked debug string.
  // Marking it up as a path says "this is a literal to copy", which is what it
  // is, without replacing the instruction with vaguer prose.
  function setBanner(...parts) {
    const b = $('banner');
    b.textContent = '';
    const filled = parts.filter((p) => p && (typeof p !== 'string' || p.length));
    if (!filled.length) {
      b.hidden = true;
      return;
    }
    for (const part of filled) {
      if (typeof part === 'string') {
        b.appendChild(document.createTextNode(part));
      } else {
        const code = document.createElement('code');
        code.className = 'banner-path';
        code.textContent = part.path;
        b.appendChild(code);
      }
    }
    b.hidden = false;
  }

  async function refreshStatus() {
    try {
      const res = await fetch('/status');
      if (!res.ok) throw new Error('HTTP ' + res.status);
      const s = await res.json();
      state.statusFailures = 0;

      // "online"/"degraded" read as a contradiction next to the app's own
      // "Runs 100% offline" footer. This field is really about whether the
      // local weights are resident, so say that.
      $('st-engine').textContent = s.llm_loaded ? 'model loaded' : 'no model';
      $('st-engine').className = s.llm_loaded ? 'ok' : 'err';
      $('st-index').textContent =
        `${formatCount(s.documents_indexed)} chunks · ${formatCount(s.files_processed)} files`;
      $('st-index').className = s.documents_indexed > 0 ? 'ok' : 'warn';
      // Same contradiction the Engine field had, one tile over: in degraded
      // mode "no model" sat next to a confidently-lit "llama3.2:3b". The name
      // is the configured model, so when it is not resident it is dimmed
      // rather than presented as the running one.
      $('st-model').textContent = s.llm_model || '—';
      $('st-model').className = s.llm_loaded ? '' : 'idle';
      $('st-embed').textContent = s.embedding_model || '—';

      // Optional ZIM library. `configured` and `reachable` are separate fields
      // for a reason: the row appears only for a deployment that asked for a
      // library, and then tells the truth about whether it answered.
      const zim = s.zim_library || {};
      const zimRow = $('st-zim-row');
      if (zimRow) zimRow.hidden = !zim.configured;
      if (zim.configured) {
        const n = zim.book_count || 0;
        $('st-zim').textContent = zim.reachable
          ? `${formatCount(n)} archive${n === 1 ? '' : 's'}`
          : 'not reachable';
        $('st-zim').className = zim.reachable && n > 0 ? 'ok' : 'warn';
      }
      $('st-version').textContent = 'v' + (s.version || '?');
      $('sidebar-footer').title = s.uptime_seconds
        ? 'Server up ' + formatUptime(s.uptime_seconds)
        : '';

      if (!s.llm_loaded) {
        setPill('err', 'LLM missing');
        setBanner(
          'Language model not loaded — drop the GGUF files into ',
          { path: s.gguf_dir || 'gguf_models/' },
          ' and they load automatically within ~30s (no restart needed). ' +
          'The library stays browsable; answers are unavailable.'
        );
      } else if (s.documents_indexed === 0) {
        setPill('warn', 'Index empty');
        setBanner();
      } else {
        setPill('ok', 'Ready', `${formatCount(s.files_processed)} docs`);
        setBanner();
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
          a.innerHTML =
            `<span class="lib-name">${escapeHtml(displayTitle(f.filename))}</span>` +
            `<span class="lib-chunks">${f.status === 'skipped' ? 'skipped' : f.chunks}</span>`;
          list.appendChild(a);
        });
      });
    } catch (err) {
      /* sidebar stays as-is on failure */
    }
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

  // The full placeholder is 38 characters and overflowed the narrow composer,
  // so it read as "Ask an emergency or survival (" — a clipped string, not a
  // prompt. CSS cannot swap placeholder text, so match the viewport here.
  function syncPlaceholder() {
    const narrow = window.matchMedia('(max-width: 620px)').matches;
    $('user-input').placeholder = narrow
      ? 'Ask a survival question…'
      : 'Ask an emergency or survival question…';
  }

  document.addEventListener('DOMContentLoaded', () => {
    initTheme();
    syncPlaceholder();
    window.matchMedia('(max-width: 620px)').addEventListener('change', syncPlaceholder);
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

    $('user-input').focus();

    refreshStatus();
    refreshLibrary();
    setInterval(refreshStatus, 8000);
  });
})();
