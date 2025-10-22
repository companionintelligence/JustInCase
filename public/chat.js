// Chat functionality for Just In Case
let isProcessing = false;

function initializeChat() {
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

function checkSystemStatus() {
  fetch('/status')
    .then(response => response.json())
    .then(data => {
      const statusDiv = document.getElementById('system-status');
      if (!statusDiv) {
        const div = document.createElement('div');
        div.id = 'system-status';
        div.style.cssText = 'position: fixed; top: 10px; right: 10px; background: #333; color: white; padding: 10px; border-radius: 5px; font-size: 12px; display: flex; align-items: center; gap: 10px;';
        document.body.appendChild(div);
      }
      
      const status = data.ingestion;
      let statusHTML = `<span>📚 ${data.documents_indexed} docs</span>`;
      
      if (status && status.in_progress) {
        const percent = data.progress_percent || 0;
        statusHTML += `
          <span style="display: flex; align-items: center; gap: 5px;">
            <span>⏳ Ingesting:</span>
            <div style="width: 100px; height: 8px; background: #555; border-radius: 4px; overflow: hidden;">
              <div style="width: ${percent}%; height: 100%; background: #4CAF50; transition: width 0.3s;"></div>
            </div>
            <span>${percent}%</span>
          </span>
        `;
        if (status.current_file) {
          statusHTML += `<span style="font-size: 11px; opacity: 0.8;">${status.current_file}</span>`;
        }
      } else if (data.documents_indexed === 0) {
        statusHTML += `<span style="color: #ff9800;">⚠️ No documents indexed yet</span>`;
      } else {
        statusHTML += `<span style="color: #4CAF50;">✓ Ready</span>`;
      }
      
      document.getElementById('system-status').innerHTML = statusHTML;
    })
    .catch(err => console.error('Failed to check status:', err));
}

function addBotMessage(text) {
  const messagesContainer = document.getElementById('chat-messages');
  const messageDiv = document.createElement('div');
  messageDiv.className = 'message bot';
  messageDiv.textContent = text;
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
  
  let html = '<strong>Sources:</strong>';
  sources.forEach(source => {
    const fileUrl = '/pdfs/' + source.filename;
    html += `
      <div class="source-item">
        <a href="${fileUrl}" target="_blank">${source.filename}</a>
        <p style="margin: 0.25rem 0 0 0; color: var(--text-secondary);">${source.text.substring(0, 150)}...</p>
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
  
  // Send to API
  fetch('/query', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({query: message})
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

    return data;
  })
  .then(data => {
    hideTypingIndicator();
    addBotMessage(data.answer);
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
});
