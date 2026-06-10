// Chat functionality for Just In Case
let isProcessing = false;
let conversationId = null;
let useContext = true; // Toggle for using document context

// Escape text destined for innerHTML interpolation
function escapeHtml(text) {
  return String(text)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

// Simple markdown → HTML renderer (no external deps)
function renderMarkdown(text) {
  let html = text
    // Escape HTML entities first
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    // Code blocks (``` ... ```)
    .replace(/```(\w*)\n([\s\S]*?)```/g, '<pre><code>$2</code></pre>')
    // Inline code
    .replace(/`([^`]+)`/g, '<code>$1</code>')
    // Bold
    .replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>')
    // Italic
    .replace(/\*(.+?)\*/g, '<em>$1</em>')
    // Headings (### before ##)
    .replace(/^### (.+)$/gm, '<h4>$1</h4>')
    .replace(/^## (.+)$/gm, '<h3>$1</h3>')
    .replace(/^# (.+)$/gm, '<h2>$1</h2>')
    // Unordered lists
    .replace(/^[\-\*] (.+)$/gm, '<li>$1</li>')
    // Numbered lists
    .replace(/^\d+\. (.+)$/gm, '<li>$1</li>')
    // Wrap consecutive <li> in <ul>
    .replace(/((?:<li>.*<\/li>\n?)+)/g, '<ul>$1</ul>')
    // Line breaks (double newline → paragraph break)
    .replace(/\n\n/g, '</p><p>')
    // Single newlines → <br>
    .replace(/\n/g, '<br>');
  return '<p>' + html + '</p>';
}

// Generate a unique conversation ID
function generateConversationId() {
  return 'conv_' + Date.now() + '_' + Math.random().toString(36).substr(2, 9);
}

function initializeChat() {
  // Initialize conversation ID
  conversationId = generateConversationId();
  
  addBotMessage("Hello! I'm your emergency knowledge assistant. I can help you with survival tips, emergency procedures, medical advice, and preparedness information. What would you like to know?");
  
  // Add suggested prompts
  const prompts = [
    "What are basic emergency preparedness steps?",
    "How do I purify water?",
    "First aid for burns",
    "72-hour emergency kit checklist"
  ];
  
  const promptsContainer = document.getElementById('suggested-prompts');
  prompts.forEach(prompt => {
    const button = document.createElement('button');
    button.className = 'suggested-prompt';
    button.textContent = prompt;
    button.onclick = () => {
      document.getElementById('user-input').value = prompt;
      sendMessage();
    };
    promptsContainer.appendChild(button);
  });
  
  // Check system status periodically
  checkSystemStatus();
  setInterval(checkSystemStatus, 5000);
}

let statusErrorLogged = false;

function ensureStatusDiv() {
  let statusDiv = document.getElementById('system-status');
  if (!statusDiv) {
    statusDiv = document.createElement('div');
    statusDiv.id = 'system-status';
    statusDiv.setAttribute('role', 'status');
    statusDiv.style.cssText = 'position: fixed; bottom: 10px; right: 10px; background: #333; color: white; padding: 10px; border-radius: 5px; font-size: 12px; display: flex; align-items: center; gap: 10px; z-index: 1000;';
    document.body.appendChild(statusDiv);
  }
  return statusDiv;
}

function checkSystemStatus() {
  fetch('/status')
    .then(response => {
      if (!response.ok) throw new Error('HTTP ' + response.status);
      return response.json();
    })
    .then(data => {
      const statusDiv = ensureStatusDiv();
      let statusHTML = `<span>📚 ${data.documents_indexed} documents indexed</span>`;

      if (data.llm_loaded === false) {
        statusHTML += `<span style="color: #ff9800;">⚠️ LLM model missing — answers unavailable</span>`;
      } else if (data.documents_indexed === 0) {
        statusHTML += `<span style="color: #ff9800;">⚠️ Loading knowledge base...</span>`;
      } else {
        statusHTML += `<span style="color: #4CAF50;">✓ Ready</span>`;
      }

      statusDiv.innerHTML = statusHTML;
      statusErrorLogged = false;
    })
    .catch(err => {
      ensureStatusDiv().innerHTML = `<span style="color: #ff9800;">⚠️ Server unreachable</span>`;
      if (!statusErrorLogged) {
        console.warn('Failed to check status:', err.message);
        statusErrorLogged = true;
      }
    });
}

function addBotMessage(text) {
  const messagesContainer = document.getElementById('chat-messages');
  const messageDiv = document.createElement('div');
  messageDiv.className = 'message bot';
  messageDiv.innerHTML = renderMarkdown(text);
  messagesContainer.appendChild(messageDiv);
  scrollToBottom();
}

function addUserMessage(text) {
  const messagesContainer = document.getElementById('chat-messages');
  const messageDiv = document.createElement('div');
  messageDiv.className = 'message user';
  messageDiv.textContent = text;
  messagesContainer.appendChild(messageDiv);
  scrollToBottom();
}

function addSourcesMessage(sources) {
  if (!sources || sources.length === 0) return;
  
  const messagesContainer = document.getElementById('chat-messages');
  const messageDiv = document.createElement('div');
  messageDiv.className = 'message sources';
  
  let html = '<strong>📚 Sources Used:</strong>';
  sources.forEach(source => {
    const fileUrl = '/sources/' + encodeURI(source.filename);
    const score = source.score ? ` (${Math.round(source.score * 100)}% relevant)` : '';
    html += `
      <div class="source-item">
        <a href="${escapeHtml(fileUrl)}" target="_blank" rel="noopener" title="Click to view PDF">📄 ${escapeHtml(source.filename)}</a>${score}
        <p style="margin: 0.25rem 0 0 0; color: var(--text-secondary); font-size: 0.85em;">${escapeHtml(source.text)}</p>
      </div>
    `;
  });
  
  messageDiv.innerHTML = html;
  messagesContainer.appendChild(messageDiv);
  scrollToBottom();
}

function showTypingIndicator() {
  document.getElementById('typing-indicator').style.display = 'block';
  scrollToBottom();
}

function hideTypingIndicator() {
  document.getElementById('typing-indicator').style.display = 'none';
}

function scrollToBottom() {
  const messagesContainer = document.getElementById('chat-messages');
  messagesContainer.scrollTop = messagesContainer.scrollHeight;
}

function sendMessage() {
  if (isProcessing) return;
  
  const input = document.getElementById('user-input');
  const message = input.value.trim();
  if (!message) return;
  
  // Clear suggested prompts after first message
  document.getElementById('suggested-prompts').innerHTML = '';
  
  addUserMessage(message);
  input.value = '';
  
  isProcessing = true;
  document.getElementById('send-button').disabled = true;
  showTypingIndicator();
  
  // Send to API with conversation ID and context preference
  fetch('/query', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({
      query: message,
      conversation_id: conversationId,
      use_context: useContext
    })
  })
  .then(async response => {
    const contentType = response.headers.get("content-type") || "";
    const isJson = contentType.includes("application/json");
    let data;

    if (isJson) {
      data = await response.json();
    } else {
      const text = await response.text();
      throw new Error("Server returned non-JSON: " + text);
    }

    if (!response.ok) {
      console.error("Server returned error:", data);
      throw new Error(data.error || "Unknown error");
    }

    console.log("Raw server response:", data); // Add debug logging
    return data;
  })
  .then(data => {
    hideTypingIndicator();
    console.log("Received response:", data); // Debug log
    
    if (data.answer && data.answer.trim()) {
      addBotMessage(data.answer);
    } else {
      console.warn("Empty or missing answer in response:", data);
      addBotMessage("I received an empty response. Please try rephrasing your question.");
    }
    
    if (data.matches && data.matches.length > 0) {
      addSourcesMessage(data.matches);
    }
    
    // Update status if included in response
    if (data.status) {
      checkSystemStatus();
    }
  })
  .catch(err => {
    hideTypingIndicator();
    console.error("Error:", err);
    addBotMessage("I'm sorry, I encountered an error. Please try again. Error: " + err.message);
  })
  .finally(() => {
    isProcessing = false;
    document.getElementById('send-button').disabled = false;
    document.getElementById('user-input').focus();
  });
}

// Initialize on load
document.addEventListener('DOMContentLoaded', () => {
  initializeChat();
  document.getElementById('user-input').focus();
  
  // Add a "New Conversation" button
  const newConvButton = document.createElement('button');
  newConvButton.textContent = 'New Conversation';
  newConvButton.style.cssText = 'position: fixed; bottom: 10px; left: 10px; padding: 8px 16px; background: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer; z-index: 1000;';
  newConvButton.onclick = () => {
    if (confirm('Start a new conversation? Current conversation history will be cleared.')) {
      conversationId = generateConversationId();
      document.getElementById('chat-messages').innerHTML = '';
      initializeChat();
    }
  };
  document.body.appendChild(newConvButton);
  
  // Set up header context toggle
  const headerToggle = document.getElementById('header-context-toggle');
  if (headerToggle) {
    headerToggle.addEventListener('change', (e) => {
      useContext = e.target.checked;
      const status = useContext ? 'enabled' : 'disabled';
      addBotMessage(`Document context ${status}. I'll ${useContext ? 'search documents to help answer your questions' : 'respond directly without searching documents'}.`);
    });
  }
});
