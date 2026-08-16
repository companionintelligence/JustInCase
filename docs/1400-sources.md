# High-Value Data Sources & Manifest Architecture

> **JustInCase (JIC)** is designed to operate completely offline during infrastructure failure, network blackouts, or remote deployments.

This document details the active 115-source catalog specified in [`sources.yaml`](../sources.yaml), how datasets are categorized, and how large-scale content archives (Kiwix ZIMs and KA-Lite media packs) are distributed via BitTorrent and tiered profiles.

---

## 1. Catalog Tier Profiles

Content acquisition in JIC is organized into tiered profiles in [`helper-scripts/fetch-source-data.sh`](../helper-scripts/fetch-source-data.sh):

| Profile | Target Footprint | Contents & Purpose | Fetch Command |
|---|---|---|---|
| **`core`** *(default)* | ~350 MB | Curated emergency PDFs and TXTs across all 9 primary functional categories. High priority, license-clean, fast download. | `./helper-scripts/fetch-source-data.sh --profile core` |
| **`emergency-zims`** | ~20 GB | High-utility Kiwix `.zim` archives (MDWiki, Ready.gov, iFixit Repair Guides, Wikipedia Medicine, MedlinePlus, FAS Military Medicine, Sustainability). | `./helper-scripts/fetch-source-data.sh --profile emergency-zims` |
| **`full-zims`** | ~240 GB | Full-scale encyclopedic archives (English Wikipedia Maxi 110 GB, Project Gutenberg 77 GB / 70k books, WikiHow Complete 51 GB, TED Talks). | `./helper-scripts/fetch-source-data.sh --profile full-zims` |
| **`kalite`** | ~40 GB | Khan Academy Lite (KA-Lite) consolidated educational media library (15,414 instructional MP4 videos and thumbnails). | `./helper-scripts/fetch-source-data.sh --profile kalite` |
| **`all`** | ~300 GB | Complete offline survival, medical, engineering, and human knowledge repository. | `./helper-scripts/fetch-source-data.sh --profile all` |

---

## 2. Active Manifest Structure (115 Verified Sources)

### `100_Survival` — Survival & Disaster Preparedness (17 sources)
* **FEMA Emergency Supply Checklist** & **Financial First Aid Kit** (Ready.gov / FEMA)
* **US Army FM 21-76 Survival Manual** & **FM 21-76-1 Survival, Evasion, Recovery**
* **US Army FM 3-25.26 Map Reading & Land Navigation**
* **Nuclear War Survival Skills** (Cresson H. Kearney, Oak Ridge National Laboratory)
* **NWS Weather Spotter's Field Guide** (NOAA / National Weather Service)
* **EPA Emergency Disinfection of Drinking Water** & **Household Wells**
* **FM 5-103 Survivability** (Field fortifications, bunkers, blast shelters)
* **FEMA CERT Basic Training Manual** (Community Emergency Response Team)
* **TC 21-3 Cold Weather Survival** & **AFM 64-5 Aircrew Search & Rescue**
* **FEMA P-320 Taking Shelter From The Storm**
* Historical & wilderness field guides (Camp Life in the Woods, Shelters & Shanties)

### `200_Medical` — Austere, Tactical & Emergency Medicine (17 sources)
* **Where There Is No Doctor / Dentist / Women Have No Doctor** (Hesperian Health Guides)
* **US Army FM 4-25.11 First Aid** & **TC 4-02.3 Field Hygiene and Sanitation**
* **Emergency War Surgery, 5th Edition** (Borden Institute)
* **Survival and Austere Medicine: An Introduction, 3rd Ed.** (Austere Medicine Group)
* **Psychological First Aid (PFA) Field Operations Guide** (NCTSN / NCPTSD)
* **Special Operations Forces Medical Handbook (ST 31-91B)**
* **US Public Health Emergency Childbirth Manual**
* **FM 8-51 Combat Stress Control** & **FM 8-285 Chemical/Nuclear Casualty Treatment**
* **AMEDD Field Dental Emergencies** & **US Navy Diving Medicine Rev 7**

### `300_Food` — Food Production, Agriculture & Preservation (14 sources)
* **USDA Complete Guide to Home Canning** (2015 revision, all 7 guides)
* **Manual of Gardening, 2nd ed.** (L.H. Bailey)
* **Peace Corps ICE Manual Series**:
  * *Traditional Field Crops* (M-13)
  * *Small Farm Crop Production* (M-4)
  * *Soils, Crops and Fertilizer Use*
  * *Small Farm Grain Storage* (M-2)
  * *Animal Traction* (M-12)
  * *Freshwater Fish Pond Culture and Management*
  * *Small Scale Beekeeping*
* **USDA Commercial & Homestead Storage of Garden Produce, Fruits, Meat & Poultry**
* **FAO Edible Wild Plants** & **USDA Nutritive Value of Foods (HB 72)**

### `400_Engineering` — Water, Power, Sanitation & Mechanics (23 sources)
* **NREL Photovoltaic Solar Resource of the United States**
* **VITA Village Technology Handbook** (Appropriate Technology Library)
* **Handbook of Gravity-Flow Water Systems** (Jordan) & **Solar Disinfection of Water (SODIS)**
* **US Army Technical Manuals (TM)**:
  * *TM 9-243 Use and Care of Hand Tools and Measuring Tools*
  * *TM 9-237 Welding Theory and Application*
  * *TC 9-524 Fundamentals of Machine Tools*
  * *FM 5-499 Hydraulics* & *FM 5-424 Theater of Operations Electrical Systems*
  * *FM 5-426 Carpentry* & *FM 5-125 Rigging Techniques*
  * *FM 5-420 Plumbing and Pipefitting*
  * *FM 5-430 Roads, Airfields, and Heliports*
  * *FM 5-480 Field Water Supply (Well Drilling & Purification)*
  * *FM 5-412 Project Management & Construction Engineering*
  * *TM 5-685 Operation, Maintenance and Repair of Generators*
* **US Navy Rate Training Manuals**: *Tools and Their Uses*, *Construction Electrician 3 & 2*
* **VITA Small-Scale Biogas Digester Design**
* **Bureau of Reclamation**: *FIST 3-30 Transformer Maintenance*
* **EPA Point-of-Use Water Treatment** & **DOE Small Wind Electric Systems**
* **US Navy Rate Training Manuals**: *Tools and Their Uses*, *Construction Electrician 3 & 2*
* **Bureau of Reclamation**: *FIST 3-30 Transformer Maintenance*
* **EPA Point-of-Use Water Treatment** & **DOE Small Wind Electric Systems**

### `500_Comms` — Emergency Communications & Radio (6 sources)
* **ARRL Emergency Communications Handbook**
* **CISA National Interoperability Field Operations Guide (NIFOG v2.02)**
* **CISA Auxiliary Communications Field Operations Guide (AUXFOG)**
* **FCC 47 CFR Part 97 — Amateur Radio Service Rules**
* **US Army FM 24-18 Tactical Single-Channel Radio Communications Techniques**
* **US Army FM 24-19 Radio Operator's Handbook**

### `600_Education` — Open Science & Math Textbooks (4 sources)
* **OpenStax Elementary Algebra 2e**
* **OpenStax Anatomy and Physiology 2e**
* **OpenStax College Physics 2e**
* **OpenStax Chemistry 2e**

### `700_Social` & `800_Software` — Civic Foundations & Technical References (8 sources)
* **Universal Declaration of Human Rights**, **US Constitution**, **The Federalist Papers**, **King James Bible**
* **Think Python 2e**, **Think OS**, **Eloquent JavaScript 3e**, **Pro Git 2e**

### `900_Transport` — Vehicles, Marine & Mobility (7 sources)
* **TM 9-8000 Principles of Automotive Vehicles** & **FM 21-305 Wheeled Vehicle Driver**
* **FM 20-22 Vehicle Recovery Operations**
* **NAVEDTRA 14050 Construction Mechanic, Advanced**
* **USCG Boat Crew Handbooks**: *Seamanship Fundamentals* & *Navigation and Piloting*
* **Bicycle Repairing** (S.D.V. Burr)

---

## 3. Large-Scale Offline Media Archives

### Kiwix `.zim` Archives (Direct Upstream Torrents)
All official Kiwix `.zim` archives are mirrored and seeded upstream at `https://download.kiwix.org/zim/`. JIC indexes official `.torrent` files for:
* **Medical:** MDWiki (8.6 GB), Wikipedia Medicine (1.8 GB), MedlinePlus (1.8 GB), FAS Military Medicine (81 MB), Global Health Medicine (70 MB)
* **Survival & Repair:** Ready.gov ZIM (4.9 GB), iFixit Manuals (3.2 GB), Sustainability StackExchange (20 MB), OSM Wiki (932 MB)
* **Comprehensive Encyclopedias:** English Wikipedia Maxi (110 GB), Project Gutenberg (77 GB), WikiHow Complete (51 GB), TED Talks (1.3 GB)

### KA-Lite Flat Media Package
The complete **40 GB** repository of Khan Academy Lite videos and thumbnails (15,414 files) is packaged as a standalone torrent generated by JIC's internal BitTorrent creator:
* **Torrent Generator Tool:** [`helper-scripts/create-torrent.py`](../helper-scripts/create-torrent.py)
* **Generated Torrent File:** `kalite_content_pack.torrent` (Info Hash: `8b119022fc33a092f17a4159d6c22b59da3664bd`)

---

## 4. Benchmark Projects & Reference Catalogs

JIC's catalog is benchmarked against leading offline and disaster-preparedness initiatives:
1. **[Project NOMAD](https://www.projectnomad.us)** — Offline server combining Kiwix, Ollama, OpenStreetMap, and Kolibri.
2. **[PrepperDisk](https://prepperdisk.com)** — Commercial curated offline encyclopedia and military field manual archive.
3. **[Internet-in-a-Box (IIAB)](https://internet-in-a-box.org)** — Raspberry Pi based offline knowledge distribution hotspot.
4. **[RACHEL by World Possible](https://rachel.worldpossible.org)** — Remote educational and medical hotspot.
5. **[CD3WD](https://en.wikipedia.org/wiki/CD3WD)** & **[Appropriate Technology Library](https://villageearth.org)** — Low-tech, off-grid civilizational rebuilding libraries.
