Process Race - Concurrent Board Game Server
Description
Process Race is a custom-built board game server designed to handle multiple concurrent players. Built entirely in C, this project focuses on robust system-level programming, utilizing Inter-Process Communication (IPC) and precise process scheduling to manage game state and player interactions without relying on external network protocols.

Tech Stack

Language: C

Architecture: Concurrent Multi-process / Multi-threaded

Core Concepts: Single Machine Mode, Inter-Process Communication (IPC), Shared Memory, Process Synchronization

Key Features & Technical Highlights

Single Machine Architecture: Engineered specifically to run in single machine mode, optimizing local resource management.

Custom IPC Implementation: Replaced traditional TCP/IP networking with efficient local Inter-Process Communication mechanisms to handle rapid data exchange between the server and player processes.

Shared Memory Management: Utilized shared memory segments for low-latency game state updates and data consistency across concurrent users.

Concurrency & Scheduling: Implemented strict multi-threaded scheduling and synchronization (e.g., mutexes/semaphores) to prevent race conditions during simultaneous player actions.

Getting Started

Prerequisites

GCC Compiler

Linux/Unix Environment (or WSL on Windows)

Compilation and Execution

Bash


# Clone the repository
git clone https://github.com/yourusername/Process-Race.git

# Navigate to the directory
cd Process-Race

# Compile the server and client components
gcc server.c -o server -pthread
gcc client.c -o client -pthread

# Run the server (initialize the game)
./server

# In separate terminal windows, run the clients
./client
