Tema2 - Protocolul BitTorrent

PEER
        Initializarea consta in citirea fisierului de intrare asociat peer-ului 
pentru a determina fisierele pe care le detine. Apoi comunica cu tracker-ul 
pentru a trimite informatii despre fisierele detinute.
    Tracker-ul va folosi aceste informatii pentru a sti ce fisiere sunt disponibile 
in retea, cate segmente au, ordinea segmentelor, hash-urile segmentelor si cine 
le detine initial. Astepta unui raspuns de tip ACK de la tracker, care confirma 
ca peer-ul poate continua operatiile.
    Dupa primirea confirmarii (ACK) de la tracker, peer-ul incepe procesul de 
descarcare a fisierelor dorite. 

TRACKER

    La initializare, trackerul asteapta sa primeasca de la fiecare client o lista 
de fisiere detinute. Trackerul adauga fiecare fisier nou in lista sa interna 
tracker_data.files. Fiecare client care detine segmente dintr-un fisier este 
adaugat in swarmul corespunzator.
    Dupa ce trackerul proceseaza toate informatiile de la clienti, acesta trimite 
cate un mesaj ACK catre fiecare client folosind MPI_Isend. Mesajul indica faptul 
ca initializarea s-a incheiat si clientii pot incepe procesul de descarcare a 
fisierelor.
    Trackerul ruleaza un ciclu infinit pentru a gestiona mesajele primite de la 
clienti. Aceste mesaje pot fi de mai multe tipuri, fiecare fiind tratat diferit:
        - Mesaj de finalizare descarcare de catre un client ("Done")
            Trackerul incrementeaza contorul completed_clients.
Daca toti clientii au terminat descarcarile, trackerul:
Creeaza un mesaj de tip finalizare folosind o structura query_request.
Trimite fiecarui client mesajul de finalizare folosind MPI_Send.
incheie executia sa.

        - Mesaj de finalizare a descarcarii unui fisier ("I'm seed")
            Trackerul marcheaza clientul care a trimis mesajul ca fiind un seed 
pentru fisierul descarcat. Aceasta actualizare poate fi realizata prin adaugarea 
clientului in swarm-ul fisierului respectiv.

        - Cerere pentru fisier (nume fisier trimis de client)
            Trackerul verifica daca fisierul solicitat exista in lista sa 
(tracker_data.files).Daca fisierul exista: Trimite informatiile despre swarm-ul 
fisierului catre clientul solicitant. Marcheaza clientul solicitant ca peer 
(daca nu este deja prezent in swarm).


DOWNLOAD_THREAD_FUNC
    Pentru toate fisierele dorite de client, functia download_thread_func 
implementeaza procesul de descarcare a segmentelor folosind o strategie de 
tip "round robin" pentru a selecta peer-urile din care se descarca segmentele. 
Aceasta abordare optimizeaza utilizarea resurselor din retea si asigura 
distributia uniforma a cererilor catre diferiti clienti, evitand supraincarcarea 
unui singur nod.
    Procesul incepe prin parcurgerea listei de fisiere dorite, fiecare nume de 
fisier fiind trimis trackerului folosind o comunicare sincrona (MPI_Sendrecv). 
Trackerul returneaza informatiile despre fisier, inclusiv numarul de segmente 
si hash-urile acestora, precum si lista de peers care detin segmentele respective. 
Aceste informatii sunt apoi utilizate pentru a adauga fisierul dorit in lista 
de fisiere detinute de client.
    Odata ce fisierul este adaugat la lista detinute, incepe procesul de descarcare 
a segmentelor. Clientul utilizeaza un index al peer-urilor, mentinut pentru fiecare 
fisier dorit, pentru a selecta peer-ul urmator din lista de swarm. Acest index 
creste dupa fiecare cerere, asigurand rotirea cererilor intre peers, ceea ce 
contribuie la echilibrarea incarcarii in retea si la imbunatatirea eficientei generale.
    Pentru fiecare segment care nu a fost inca descarcat, clientul creeaza o 
cerere de tip query_request, care include identificatorul peer-ului, numele 
fisierului si hash-ul segmentului dorit. Cererea este trimisa peer-ului selectat 
folosind MPI_Sendrecv. Peer-ul raspunde cu un mesaj, fie "ACK" (indica faptul ca 
segmentul a fost gasit si descarcat), fie un alt mesaj, daca segmentul nu este 
disponibil. in cazul in care segmentul este descarcat cu succes, acesta este 
marcat ca descarcat in structura locala, iar numarul total de segmente descarcate 
este incrementat. Dupa fiecare 10 de segmente descarcate, clientul solicita 
trackerului o actualizare a informatiilor despre swarm, pentru a se asigura 
ca lista de peers este actualizata si include eventuale noi surse.
    Dupa ce toate segmentele unui fisier au fost descarcate, clientul trimite 
trackerului un mesaj indicand ca a devenit un seed pentru acel fisier, semnaland 
ca poate furniza segmentele acestui fisier catre alti clienti. in continuare, 
fisierul complet descarcat este salvat intr-un fisier de iesire local, denumit 
folosind formatul client<RANK>_<NUME_FISIER> pentru identificare.
    Dupa ce toate fisierele dorite au fost procesate, clientul notifica trackerul 
ca a finalizat descarcarea tuturor fisierelor trimitand mesajul "Done". 
in final, toate tipurile MPI utilizate in proces sunt eliberate pentru a 
preveni scurgerile de resurse.

UPLOAD_THREAD_FUNC

    Ruleaza incontinuu ascultand cereri din partea altor clienti, implementand 
logica unui upload thread care furnizeaza segmentele fisierelor detinute de acest client.
    La inceput, se creeaza un tip MPI pentru structura query_request, care 
defineste formatul mesajelor de cerere trimise de alti clienti. Thread-ul continua 
sa ruleze intr-un loop infinit, utilizand MPI_Probe pentru a detecta prezenta unui 
mesaj. Acest pas permite thread-ului sa determine caracteristicile mesajului (sursa 
si tag-ul) inainte de a-l primi efectiv.
    Cand se primeste un mesaj cu MPI_TAG egal cu 999, acesta reprezinta o cerere 
pentru un segment al unui fisier detinut de client. Cererea contine numele fisierului, 
hash-ul segmentului solicitat si identificatorul peer-ului care face cererea. Thread-ul 
verifica daca segmentul solicitat este disponibil, iterand prin lista de fisiere 
detinute. Daca gaseste segmentul si acesta este marcat ca descarcat (downloaded[j] == 1), 
raspunde cu mesajul "ACK". Daca segmentul nu este gasit sau nu este disponibil, raspunde 
cu "NoACK". Mesajul de raspuns este trimis inapoi catre clientul care a facut cererea 
folosind MPI_Send.
    In cazul in care este primit un mesaj cu MPI_TAG egal cu 8, acesta semnifica 
faptul ca tracker-ul a notificat toti clientii sa inchida upload thread-urile si 
sa se opreasca. in acest caz, thread-ul primeste mesajul si iese din bucla principala, 
returnand controlul functiei apelante.
