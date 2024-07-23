## Examples

In this folder, you can find examples where the Portals functions are used.

#

* **init_queue** : show the initalization of the interface, setup a buffer for incoming messages and get an event into the event queue.

* **put_to_self** : show the sending of a message to oneself and handle completion events.

* **put_to_self_logical** : desmonstrate the sending of a message and its ack using a logical interface. 

* **hello** : send an hello world from a client to a server (physical addressing). See the source files for a detailed usage.
    - client : send a message and receive it's ack. Usage: `client <server nid> <message to send>`
    - server : receive the message and read it. Usage `server [number of messages to wait]`

* **search** : example to use PtlMESearch.
