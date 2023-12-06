# P2Streaming
P2Streaming is a system of streaming media content, such as videos or audio, over a peer-to-peer network. P2Streaming can support both Video on Demand (VOD) and live streaming scenarios in a Peer-to-Peer (P2P) context. In traditional client-server streaming, the content is delivered from a central server to multiple clients. In P2Streaming, the content is distributed among multiple peers (users) who simultaneously receive and relay the content to other peers.

P2Streaming leverages the collective resources of the peers in the network, such as their upload bandwidth and processing power, to distribute the streaming workload. Each peer in the network acts as both a receiver and a sender of data. When a peer receives a portion of the media content, it can relay that portion to other peers who request it, reducing the strain on the central server and improving scalability.

P2Streaming can provide several benefits, including:

* Scalability: By distributing the streaming workload among multiple peers, P2Streaming can handle increased demand without relying solely on a central server.

* Redundancy: Since the content is distributed across multiple peers, P2Streaming can provide resilience against failures or network disruptions. If one peer becomes unavailable, other peers can continue to relay the content.

* Lower server bandwidth costs: With P2Streaming, the server's bandwidth requirements can be reduced since the content is shared among peers. This can be particularly advantageous for popular or bandwidth-intensive content.

* Faster streaming start time: P2Streaming can enable faster start times for streaming media since the content can be retrieved from multiple sources simultaneously.

The repository contains the server-side, client-side, and multi-cloud remote transmission components of the system.
