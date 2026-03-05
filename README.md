Rusu Horia - 321 CD
README - Platforma mesajerie client-server TCP si UDP

1. Descriere generala
   Acest proiect implementeaza o platforma de mesagerie in model client-server folosind socket-uri TCP pentru clientii subscriber si UDP pentru clientii publisher. Serverul actioneaza ca broker, primind mesaje UDP, filtrandu-le dupa topic si retransmitandu-le catre toti clientii TCP abonati.

2. Structura proiect
   server.c : implementarea broker-ului, gestioneaza conexiuni TCP, receptie UDP, multiplexare I/O si administrare abonari
   subscriber.c: client TCP (subscriber) care se conecteaza la server, trimite comenzi subscribe/unsubscribe si afiseaza mesajele primite
   Makefile : contine target-urile server, subscriber si clean

3. Protocol aplicativ TCP (incadrare mesaje)
   Pentru delimitarea corecta a mesajelor peste TCP, am definit un header fix de 4 octeti in network byte order care indica lungimea payload-ului urmator. Structura transmisiilor este:
   4 octeti (uint32\_t net\_len) urmat de net\_len octeti de payload
   unde net\_len este lungimea in octeti a payload-ului, iar payload contine comanda (de ex. subscribe topic) sau mesajul servit de broker.
   Acest framing asigura separarea corecta chiar daca TCP concateneaza sau fragmenteaza datele.

4. Multiplexare I/O si optimizari TCP
   Serverul foloseste select() pentru a asculta simultan socket-ul de ascultare TCP, socket-ul UDP, stdin si toti clientii TCP activi. Toate conexiunile TCP dezactiveaza algoritmul Nagle prin optiunea TCP\_NODELAY pentru latenta redusa. Buffer-ul de iesire standard (stdout) este dezactivat (setvbuf cu \_IONBF) atat in server, cat si in subscriber pentru feedback imediat.

5. Gestionare abonari si wildcard-uri
   Fiecare client TCP detine o lista dinamica de pattern-uri pentru topicuri, de forma UPB/precis/*/value sau +/status. Functia match\_topic() implementeaza suport pentru '*' (orice niveluri) si '+' (un nivel) folosind recursivitate si separarea topicului in segmente. La deconectare, abonamentele se salveaza si se restaureaza la reconectare, pe baza ID-ului clientului.

6. Compilare si rulare
   Pentru compilare rulati:
   make server
   make subscriber
   Pentru curatare:
   make clean
   Pornire server:
   ./server <PORT>
   Pornire subscriber:
   ./subscriber \<ID\_CLIENT> \<IP\_SERVER> \<PORT\_SERVER>

7. Observatii finale
   Toate apelurile de sistem sunt verificate pentru erori, aplicandu-se programare defensiva. Protocolul aplicativ TCP definit este eficient si robust, conform cerintelor temei.
