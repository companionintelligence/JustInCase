# Just In Case (JIC) - System Architecture

(ChatGPT generated)

## Overview

Just In Case is an emergency knowledge assistant that provides conversational access to a collection of PDF documents through a modern AI-powered interface. The system is designed to be self-contained, efficient, and deployable in resource-constrained environments where internet connectivity may be limited or unavailable.

## Core Architecture Principles

The architecture follows a "pure C++" approach, directly binding to the llama.cpp library without intermediate layers or language bindings. This design choice prioritizes performance, minimal dependencies, and deployment simplicity. The entire system runs as containerized services, making it portable across different environments while maintaining consistent behavior.

## Key Components

### Document Processing Pipeline

At the heart of the system is Apache Tika, running as a dedicated service for PDF text extraction. When new documents are added to the `public/sources` directory, the ingestion service automatically detects and processes them. This separation of concerns allows the main server to remain responsive while heavy document processing happens in the background. The extracted text is split into overlapping chunks of approximately 2000 characters, ensuring that context is preserved across chunk boundaries.

### Embedding Generation

The system uses Nomic Embed Text v1.5, a state-of-the-art embedding model optimized for semantic search. Running directly through llama.cpp with GGUF quantized models, it generates 768-dimensional vectors for each text chunk. These embeddings capture the semantic meaning of the text, enabling the system to find relevant passages based on meaning rather than just keyword matching.

### Vector Storage and Retrieval

Rather than using external dependencies like FAISS, we implemented a custom in-memory vector index with brute-force nearest neighbor search. While this approach may seem simplistic, it provides several advantages: zero external dependencies, complete control over the implementation, and surprisingly good performance for moderate-sized document collections. The index is persisted to disk in a simple binary format, allowing for quick startup times and data persistence across container restarts.

### Language Model Integration

The conversational interface is powered by Llama 3.2 1B Instruct, chosen for its balance of capability and resource efficiency. The model runs directly through llama.cpp, leveraging GGUF quantization to reduce memory requirements while maintaining quality. The integration includes careful prompt engineering to ensure the model synthesizes information from retrieved documents rather than simply regurgitating text.

### Web Interface

A clean, modern web interface provides the user-facing experience. Built with vanilla JavaScript and CSS, it supports both light and dark themes, displays source citations with relevance scores, and maintains conversation history. The interface includes a toggle for disabling document context, allowing users to have direct conversations with the LLM when needed.

## Architectural Decisions

### Why Pure C++?

The decision to use C++ throughout the stack eliminates the overhead of language bindings and ensures maximum performance. By directly linking against llama.cpp, we avoid the complexity and potential instability of Python bindings or HTTP APIs. This approach also reduces memory usage and startup time, critical factors in emergency scenarios.

### Custom Vector Store vs. FAISS

While FAISS offers sophisticated indexing algorithms, our custom implementation provides adequate performance for typical document collections while eliminating a complex dependency. The brute-force search is parallelized and optimized for cache locality, making it surprisingly efficient for collections up to tens of thousands of documents.

### PostgreSQL and pgvector Provisioning

Although the current implementation uses our custom vector store, we've provisioned PostgreSQL with pgvector extension for future scalability. This forward-thinking approach allows for a smooth transition to a more sophisticated vector storage solution when document collections grow beyond the efficient range of our current implementation.

### Separation of Ingestion and Serving

Running document ingestion as a separate service prevents long-running PDF processing from impacting query response times. This microservices-inspired approach also allows for different resource allocations - the ingestion service can be CPU-limited while the main server gets priority access to system resources.

## Data Flow

1. **Document Upload**: PDFs are placed in `public/sources` directory
2. **Text Extraction**: Tika service extracts text content from PDFs
3. **Chunking**: Text is split into overlapping chunks for processing
4. **Embedding**: Each chunk is converted to a 768-dimensional vector
5. **Indexing**: Vectors are added to the in-memory index and persisted
6. **Query Processing**: User questions are embedded and similar chunks retrieved
7. **Response Generation**: Retrieved chunks provide context for the LLM
8. **Citation Display**: Sources are shown with relevance scores and links

## Deployment Considerations

The entire system is containerized using Docker, with separate containers for the main server, ingestion service, and Tika. This approach ensures consistent deployment across different environments and simplifies resource management. The use of GGUF models allows for efficient CPU-only deployment, making the system accessible on a wide range of hardware without requiring GPUs.

## Future Enhancements

The architecture is designed to evolve. The PostgreSQL/pgvector foundation allows for scaling to millions of documents when needed. The modular design makes it straightforward to swap components - for example, replacing the embedding model or upgrading to larger language models as hardware capabilities improve. The clean separation between services also enables horizontal scaling of the ingestion pipeline for processing large document collections.
