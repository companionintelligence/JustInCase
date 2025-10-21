// Emergency Preparedness Coach Logic
const prepChecklist = {
  musterPoint: { 
    question: "Do you have a designated family meeting point in case of emergency?",
    followUp: "Great! Make sure everyone knows where it is. Consider having both a nearby location (like a neighbor's house) and a distant location (like a relative's home in another town).",
    category: "Planning"
  },
  water: { 
    question: "Do you have emergency water supplies? (Recommended: 1 gallon per person per day for at least 3 days)",
    followUp: "Remember to also have water purification tablets or a way to boil water. Store water in a cool, dark place and replace every 6 months.",
    category: "Supplies"
  },
  food: { 
    question: "Do you have non-perishable food for at least 3 days?",
    followUp: "Good! Don't forget a manual can opener. Choose foods that don't require cooking or refrigeration. Check expiration dates regularly.",
    category: "Supplies"
  },
  firstAid: { 
    question: "Do you have a well-stocked first aid kit?",
    followUp: "Excellent! Make sure it includes any prescription medications you need. Consider taking a first aid course too.",
    category: "Medical"
  },
  flashlights: { 
    question: "Do you have flashlights and extra batteries?",
    followUp: "Great! Keep one in each room and check batteries every 6 months. Consider hand-crank or solar flashlights too.",
    category: "Equipment"
  },
  radio: { 
    question: "Do you have a battery-powered or hand-crank radio?",
    followUp: "Perfect! This will help you stay informed during power outages. NOAA Weather Radio is especially useful.",
    category: "Communication"
  },
  cash: { 
    question: "Do you have emergency cash in small bills?",
    followUp: "Smart thinking! ATMs and credit card machines may not work. Keep cash in a waterproof container.",
    category: "Financial"
  },
  documents: { 
    question: "Do you have copies of important documents in a waterproof container?",
    followUp: "Excellent! Include IDs, insurance policies, bank info, and medical records. Consider digital copies too.",
    category: "Documentation"
  },
  plan: { 
    question: "Does your family have a written emergency plan?",
    followUp: "Wonderful! Review it every 6 months and practice it. Include out-of-state contacts and evacuation routes.",
    category: "Planning"
  },
  localThreats: { 
    question: "Do you know the specific threats in your area? (floods, earthquakes, tornadoes, etc.)",
    followUp: "Good awareness! Make sure your kit and plan address these specific risks. Sign up for local emergency alerts.",
    category: "Awareness"
  }
};

let currentStep = 0;
let responses = {};
const checklistKeys = Object.keys(prepChecklist);

function initializeCoach() {
  addBotMessage("Hi! I'm your emergency preparedness coach. I'll help you assess your readiness for emergencies. This will only take a few minutes. Ready to start?");
  setQuickActions(["Yes, let's start!", "Tell me more first"]);
}

function addBotMessage(text) {
  const messagesContainer = document.getElementById('chat-messages');
  const messageDiv = document.createElement('div');
  messageDiv.className = 'message bot';
  messageDiv.textContent = text;
  messagesContainer.appendChild(messageDiv);
  messagesContainer.scrollTop = messagesContainer.scrollHeight;
}

function addUserMessage(text) {
  const messagesContainer = document.getElementById('chat-messages');
  const messageDiv = document.createElement('div');
  messageDiv.className = 'message user';
  messageDiv.textContent = text;
  messagesContainer.appendChild(messageDiv);
  messagesContainer.scrollTop = messagesContainer.scrollHeight;
}

function setQuickActions(actions) {
  const container = document.getElementById('quick-actions');
  container.innerHTML = '';
  actions.forEach(action => {
    const button = document.createElement('button');
    button.className = 'quick-action';
    button.textContent = action;
    button.onclick = () => {
      document.getElementById('user-input').value = action;
      sendMessage();
    };
    container.appendChild(button);
  });
}

function updateProgress() {
  const progress = (currentStep / checklistKeys.length) * 100;
  document.getElementById('progress-bar').style.width = progress + '%';
  document.getElementById('progress-text').textContent = Math.round(progress) + '% Complete';
}

function sendMessage() {
  const input = document.getElementById('user-input');
  const message = input.value.trim();
  if (!message) return;
  
  addUserMessage(message);
  input.value = '';
  
  // Process the message
  setTimeout(() => processResponse(message), 500);
}

function processResponse(message) {
  const lowerMessage = message.toLowerCase();
  
  // Initial start
  if (currentStep === 0 && (lowerMessage.includes('yes') || lowerMessage.includes('start'))) {
    currentStep = 1;
    askNextQuestion();
  } else if (currentStep === 0 && lowerMessage.includes('tell me more')) {
    addBotMessage("I'll ask you about 10 key areas of emergency preparedness. For each one, you can answer yes, no, or tell me more. At the end, I'll give you a personalized summary of what you have and what you might need. It's completely private - no data is stored. Ready to begin?");
    setQuickActions(["Yes, let's start!"]);
  } else if (currentStep > 0 && currentStep <= checklistKeys.length) {
    // Process checklist responses
    const key = checklistKeys[currentStep - 1];
    
    if (lowerMessage.includes('yes') || lowerMessage.includes('have')) {
      responses[key] = true;
      addBotMessage(prepChecklist[key].followUp);
      setTimeout(() => {
        currentStep++;
        askNextQuestion();
      }, 1500);
    } else if (lowerMessage.includes('no') || lowerMessage.includes("don't")) {
      responses[key] = false;
      addBotMessage("That's okay! This is something to consider adding to your emergency kit. Every step counts!");
      setTimeout(() => {
        currentStep++;
        askNextQuestion();
      }, 1500);
    } else if (lowerMessage.includes('not sure') || lowerMessage.includes('maybe')) {
      responses[key] = 'unsure';
      addBotMessage("It's good to double-check! Mark this as something to review. Better safe than sorry!");
      setTimeout(() => {
        currentStep++;
        askNextQuestion();
      }, 1500);
    } else {
      addBotMessage("I didn't quite understand. Do you have this item/plan in place?");
      setQuickActions(["Yes", "No", "Not sure"]);
    }
  }
  
  updateProgress();
}

function askNextQuestion() {
  if (currentStep > checklistKeys.length) {
    showSummary();
    return;
  }
  
  const key = checklistKeys[currentStep - 1];
  addBotMessage(prepChecklist[key].question);
  setQuickActions(["Yes", "No", "Not sure"]);
}

function showSummary() {
  addBotMessage("Great job completing the assessment! Here's your personalized emergency preparedness summary:");
  
  const summary = document.getElementById('checklist-summary');
  const itemsContainer = document.getElementById('checklist-items');
  itemsContainer.innerHTML = '';
  
  // Group by category
  const categories = {};
  checklistKeys.forEach(key => {
    const category = prepChecklist[key].category;
    if (!categories[category]) categories[category] = [];
    categories[category].push({
      key: key,
      status: responses[key],
      question: prepChecklist[key].question
    });
  });
  
  // Display by category
  Object.entries(categories).forEach(([category, items]) => {
    const categoryDiv = document.createElement('div');
    categoryDiv.innerHTML = `<h4 style="margin-top: 1rem; margin-bottom: 0.5rem;">${category}</h4>`;
    itemsContainer.appendChild(categoryDiv);
    
    items.forEach(item => {
      const itemDiv = document.createElement('div');
      itemDiv.className = 'checklist-item' + (item.status === true ? ' complete' : '');
      const icon = item.status === true ? '✓' : item.status === false ? '✗' : '?';
      itemDiv.innerHTML = `<span style="font-weight: bold;">${icon}</span> ${item.question.split('?')[0]}`;
      itemsContainer.appendChild(itemDiv);
    });
  });
  
  summary.style.display = 'block';
  
  // Calculate score
  const prepared = Object.values(responses).filter(r => r === true).length;
  const total = checklistKeys.length;
  const percentage = Math.round((prepared / total) * 100);
  
  let finalMessage = `You're ${percentage}% prepared! `;
  if (percentage >= 80) {
    finalMessage += "Excellent work! You're well-prepared for emergencies. Keep maintaining your supplies and reviewing your plans.";
  } else if (percentage >= 50) {
    finalMessage += "You're on the right track! Focus on filling the gaps in your preparedness, especially the items marked with ✗.";
  } else {
    finalMessage += "You've made a start! Emergency preparedness is a journey. Begin with the basics: water, food, and a family plan.";
  }
  
  setTimeout(() => {
    addBotMessage(finalMessage);
    addBotMessage("Would you like to search for specific information about any of these topics?");
    setQuickActions(["Go to search", "Start over"]);
  }, 1000);
}

// Handle final actions
function handleFinalAction(action) {
  if (action.includes('search')) {
    window.location.href = 'index.html';
  } else if (action.includes('start over')) {
    currentStep = 0;
    responses = {};
    document.getElementById('checklist-summary').style.display = 'none';
    document.getElementById('chat-messages').innerHTML = '';
    initializeCoach();
  }
}

// Initialize on load
document.addEventListener('DOMContentLoaded', () => {
  initializeCoach();
});

// Override processResponse for final actions
const originalProcessResponse = processResponse;
processResponse = function(message) {
  if (currentStep > checklistKeys.length) {
    handleFinalAction(message.toLowerCase());
  } else {
    originalProcessResponse(message);
  }
};
