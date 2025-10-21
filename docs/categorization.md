## Categorization

How do we organize the data so that it can be statically browsed? Can we provide multiple organizational schemes for crisis on different time scales and for different kinds of users? How is data traditionally organized?

Here are a few of the requirements we have:

- Short term versus longer term ... Information needs vary from short to medium to long-term.
- Crisis mode... Information can be hard to find quickly in a crisis (an LLM may provide critical indexability / searchability).
- Education... Readers themselves have varying education leveks, comprehension, backgrounds, languages.
- Visual Learners... Many readers consume information visually, through mixed-media, not always text.
- Biases... It's worth noting that there may be biases in how we organize by default as well - this article covers some of those observations: https://www.careharder.com/blog/systemic-injustice-in-the-dewey-decimal-system
- Updating....  Some data, like advanced medical guidelines, might change significantly over time; others (public domain literature) don’t.
- Quality over quantity.... Endless gigabytes of unorganized data can be worse than a smaller, well-indexed collection.
- Printed guides... provide step-by-step guide (even a printed manual) on how to access the data and use any software.

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

### ChatGPT's proposed scheme:

- survival
- shelter, construction, primitive, modern, energy, power, water filtering
- tools, engineering, blacksmithing, mechanics, hand tool use
- fire fuel, firemaking, fuel production
- energy and water - solar, wind etc, power generation, water management
- construction, tools, building, earthships, mechanics, engineering, maintenance
- diy skills, leatherworking, sewing, soap, candles
- bushcraft, wilderness survival

- medical, health; body and mind
- exercises, stress
- diets, nutrition
- naturopathy, medicines
- mental health
- preventative
- anatomy, surgery
- personal and mental health, resilience, psychology, group dynamics
- first aid

- food
- permaculture, farming
- livestock, aquaculture, bees
- food processing, preservation
- herbs etc
- environmental management, ecosystem restoration
- balancing ecosystems
- foraging
- animal husbandry; bee-keeping, growing mushrooms etc

- engineering
- math; abstract reasoning; computer science, programming
- structures

- communications, navigation, radio, maps, navigation, knowledge storage
- maps, emergency info,
- repair guides

- education, art, philosophy
- history, books
- drawing, music, poetry, interpretation
- rich media communication
- performing, group performances, religion, mythmaking
- morals, ethics, interpretation
- sense making, perception, value basis and refinement

- culture, community related - governance, decision making, psychology, rhizomatic goals
- transmission of knowledge and values
- individual agency, participation, alignment of values
- splitting groups
- coercion
- psyops; manipulation
- dark traits; avoid consolidation of power, rotate power
- education
- ethical frameworks
- legal documents and patterns

### Folder Hierarchy scheme proposed by ChatGPT

The raw ingestion is structure independent, but users may wish to organize files in this way:

A clear folder hierarchy may save a lot of frustration as a fallback or 'fundamental' structure. Within each folder, you could keep additional organization by topic or format (PDF, HTML, ePub, etc.). File naming should be consistent: Include descriptive titles and possibly a short version or code for the source. Use consistent date/version tags so you know how old the material is. For Example: FirstAid_USArmyFieldManual_FM4-25.11_1998.pdf . This is an example structure:

/01_Survival_Guides/
   |-- BasicSurvival/
   |-- WildernessSurvival/
   |-- Nuclear_Biological_Chemical/ (NBC preparedness)

/02_Medical_References/
   |-- FirstAid/
   |-- MedicalTextbooks/
   |-- DiseaseAndTreatment/
   |-- Veterinary/

/03_Food_Agriculture/
   |-- Crop_Growing/
   |-- Permaculture/
   |-- Aquaponics/
   |-- Food_Storage_Preservation/

/04_Engineering_Construction/
   |-- Building_Construction/
   |-- Renewable_Energy/
   |-- Water_Systems/
   |-- Off_Grid_Living/

/05_Communication_Electronics/
   |-- Amateur_Radio/
   |-- Electronics_Basics/
   |-- Solar_Power/

/06_Education/
   |-- K_12/
   |-- University_Level/
   |-- Reference_Texts/
   |-- Wikipedia_Offline/
   |-- Wiktionary/
   |-- Other_Wiki_Projects/

/07_Social_Literature_Culture/
   |-- Project_Gutenberg/
   |-- Historical_Documents/
   |-- Language_Learning/
   |-- Art

/08_Software_Tools/
   |-- Linux_ISOs/
   |-- Offline_Repositories/
   |-- Programming_Tutorials/

In more detail:

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
