// Theme management
function initTheme() {
  const savedTheme = localStorage.getItem('theme') || 'light';
  document.documentElement.setAttribute('data-theme', savedTheme);
}

function toggleTheme() {
  const currentTheme = document.documentElement.getAttribute('data-theme');
  const newTheme = currentTheme === 'light' ? 'dark' : 'light';
  document.documentElement.setAttribute('data-theme', newTheme);
  localStorage.setItem('theme', newTheme);
}

// Tab management
function switchTab(tabName) {
  // Update tab buttons
  document.querySelectorAll('.tab').forEach(tab => {
    tab.classList.remove('active');
  });
  event.target.classList.add('active');
  
  // Update tab content
  document.querySelectorAll('.tab-content').forEach(content => {
    content.classList.remove('active');
  });
  document.getElementById(`${tabName}-tab`).classList.add('active');
}

// Initialize theme on load
document.addEventListener('DOMContentLoaded', initTheme);

function search() {
  const q = document.getElementById('q').value.trim();
  const results = document.getElementById('results');
  const error = document.getElementById('error');
  results.innerHTML = "Searching...";
  error.textContent = "";

  if (!q) {
    error.textContent = "Error: Query cannot be empty.";
    results.innerHTML = "";
    return;
  }

  console.log("Sending query:", q);

  fetch('/query', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({query: q})
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
      error.textContent = "Server Error: " + (data.error || "Unknown error");
      results.innerHTML = "";
      return;
    }

    console.log("Successful response:", data);
    renderResults(data);
  })
  .catch(err => {
    console.error("Fetch or parsing error:", err);
    error.textContent = "Error: " + err.message;
    results.innerHTML = "";
  });
}

function renderResults(data) {
  const results = document.getElementById('results');
  let html = `<div class="answer">${data.answer}</div>`;

  data.matches.forEach(match => {
    const fileUrl = '/' + match.filename;
    html += `
      <div class="document">
        <a href="pdfs/${fileUrl}" target="_blank">${match.filename}</a>
        <p>${match.text}</p>
      </div>`;
  });

  results.innerHTML = html;
}

