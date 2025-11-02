## Categorization

How do we organize the data so that it can be statically browsed? Can we provide multiple organizational schemes for crisis on different time scales and for different kinds of users? How is data traditionally organized?

## Kinds of ways people would access a JIC dataset:

Here are a few of the requirements we have around how people would use a dataset like this:

- Short term versus longer term ... Information needs vary from short to medium to long-term.
- Crisis mode... Information can be hard to find quickly in a crisis (an LLM may provide critical indexability / searchability).
- Education... Readers themselves have varying education leveks, comprehension, backgrounds, languages.
- Visual Learners... Many readers consume information visually, through mixed-media, not always text.
- Biases... It's worth noting that there may be biases in how we organize by default as well - this article covers some of those observations: https://www.careharder.com/blog/systemic-injustice-in-the-dewey-decimal-system
- Updating....  Some data, like advanced medical guidelines, might change significantly over time; others (public domain literature) don’t.
- Quality over quantity.... Endless gigabytes of unorganized data can be worse than a smaller, well-indexed collection.
- Printed guides... provide step-by-step guide (even a printed manual) on how to access the data and use any software.

## Kinds of ways people consume / digest knowledge:

It's worth noting that there are different flavors of knowledge as well as different ways that people prefer to acquire knowledge:

- Explicit ... easily shared, documents, manuals, verbal instruction (a math formula).
- Tacit ... deeply embedded in personal knowledge, intuition, experience, harder to share (playing a violin).
- Procedural ... know-how or steps required to complete a task; acquired via practice (riding a bike).
- Declarative ... facts, concepts (a persons name).
- Empirical ... based on observation of the world.
- Meta-cognition; awareness of ones own thinking process and learning strategies.

The fact that we need this dataset offline also adds challenges:

- Searchability ... big offline library quickly becomes useless if you can’t find what you need. Tools like Kiwix offer an offline “Wikipedia-like” search interface for large wikis. For general PDF or HTML collections, a local search engine like DocFetcher or Apache Solr or an open-source full-text may be critical... or even a full blown LLM.

- Metadata and Summaries ... a simple plain-text or HTML “index” file at the root directory listing each major folder and describing its contents is extremely helpful. Include meta instructions for how to use the library (especially if there are special viewers, compressed file archives, or search tools). This document itself may be part of an 'about' page about the collection - a place to allow us to wrestle with the organizational concepts themselves.

## Kinds of ways people organize knowledge

### Dewey Decimal System:

000 – Computer science, information and general works
100 – Philosophy and psychology
200 – Religion
300 – Social sciences
400 – Language
500 – Pure science
600 – Technology
700 – Arts and recreation
800 – Literature
900 – History and geography

The tree-of-knowledge system (https://eschool.gpaeburgas.org/BIP/blog/2019/08/13/the-tree-of-knowledge-system/) :

1. Abstract; information theory, math, computer science, data science
2. Natural; physics, chemistry, life/biology, earth sciences, astronomy, science historical
3. Individual; neurobiology, psychology, intelligence, theory of mind
4. Social; world history, politics, economics, anthropology

### Library of Congress Classification Scheme is:

A -- GENERAL WORKS
B -- PHILOSOPHY. PSYCHOLOGY. RELIGION
C -- AUXILIARY SCIENCES OF HISTORY
D -- WORLD HISTORY AND HISTORY OF EUROPE, ASIA, AFRICA, AUSTRALIA, NEW ZEALAND, ETC.
E -- HISTORY OF THE AMERICAS
F -- HISTORY OF THE AMERICAS
G -- GEOGRAPHY. ANTHROPOLOGY. RECREATION
H -- SOCIAL SCIENCES
I
J -- POLITICAL SCIENCE
K -- LAW
L -- EDUCATION
M -- MUSIC AND BOOKS ON MUSIC
N -- FINE ARTS
O
P -- LANGUAGE AND LITERATURE
Q -- SCIENCE
R -- MEDICINE
S -- AGRICULTURE
T -- TECHNOLOGY
U -- MILITARY SCIENCE
V -- NAVAL SCIENCE
W
X
Z -- BIBLIOGRAPHY. LIBRARY SCIENCE. INFORMATION RESOURCES (GENERAL)

### An interesting proposed scheme https://thepantologist.com/classifying-all-human-knowledge/ :

1. Abstract; information theory, math, computer science, data science
2. Natural; physics, chemistry, life/biology, earth sciences, astronomy, science historical
3. Conscious; neurobiology, psychology, intelligence, theory of mind
4. Social; world history, politics, economics, anthropology


### In more detail:

1. Practical Survival and Preparedness Guides
-	First aid and medical treatment
-	Wilderness survival, food gathering, hunting/fishing, basic shelter
-	Water purification
-	Basic security strategies
2. Medical & Scientific References
-	Basic human anatomy
-	Common disease diagnoses & treatments
-	Sanitation and hygiene best practices
-	Vet care (if livestock animals are part of the plan)
3. Agriculture, Permaculture & Gardening
-	Crop growing techniques
-	Seed saving, soil management
-	Hydroponics, aquaponics, permaculture design
4. Engineering & Construction
-	Home building fundamentals (woodworking, masonry, metalworking)
-	Off-grid power generation (solar, wind, micro-hydro, biogas)
-	Water/wastewater systems
5. Communication & Electronics
-	Amateur radio and communication guides
-	Electronics repair and building
-	Solar panel repairs, battery management systems
6. Educational Materials
-	Math, science, language (K–12 to advanced textbooks)
-	PDFs of open-source or public-domain textbooks
-	Offline Wikipedia (e.g., Kiwix)
-	Project Gutenberg (public domain books)
-	Wikihow offline or other how-to websites
-	TED talks transcripts (for broader educational content)
7. Cultural & Historical Materials
-	Important historical texts, philosophical works
-	Some literature to keep people entertained and mentally engaged
8. Open-Source Software Repositories (optional)
-	Linux distros, offline package repositories
-	Basic software tools, office suites, programming languages
-	References on how to code, for those that might want to maintain or repurpose old computers
