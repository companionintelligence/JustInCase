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
