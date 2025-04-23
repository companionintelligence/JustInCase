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

