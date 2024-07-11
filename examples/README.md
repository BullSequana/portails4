## Examples

In this folder, you can find examples where the Portals functions are used.

#

* **init_queue** : show the initalization of the interface, setup a buffer for incoming messages and get an event into the event queue.

* **put_to_self** : show the sending of a message to oneself and handle completion events.

* **hello** : show the connection between a transmiter and a receiver.
    - client : send a message and receive it's ack.
    - server : receive the message and read it.

* **get_me** : show the get event from a client to a server. 
    - client2 : try to get messages from the specified server  
    - server2 : prepare a buffer with messages and put it in the priority list

* **search** : example to use PtlMESearch.

* **get_id** : example to get our different id depending of the logical/physical interface.

* **put_to_self_logical** : show the sending of a message and it's ack using a logical interface. 

* **ping_pong** : logical addressing example. We initalize two logical interfaces which communicate through put events. 
