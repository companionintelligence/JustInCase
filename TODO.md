# Oct 31 2025

- ingestion currently uses tika to convert pdf to text, but since we're using qwen let's convert to an image and then feed frames to qwen and get it to do some kind of reasonable extraction and then use that as a vector

- reduce vectorization size to ideal nomic length 512/768

- improve scripts to fetch torrents of collections such as project gutenburg

- fetch maps and other data incrementally; like osm maps; make it available not as an llm resource but just as general fodder - maybe have leaflet - can we have some kind of crude lowrez vectorized map?

- a competitor to llama.cpp is https://developers.llamaindex.ai ? can be run in a docker

- review knowledge graphs - this would be a way to have an explicit human legible knowledge tree - https://neo4j.com/blog/developer/llamaparse-knowledge-graph-documents/ ... could be useful?

- review https://medium.com/kx-systems/rag-llamaparse-advanced-pdf-parsing-for-retrieval-c393ab29891b also see https://blog.abdellatif.io/production-rag-processing-5m-documents

- review / evaluate other strategies for indexing beyond faiss - may have to go to pgvector

