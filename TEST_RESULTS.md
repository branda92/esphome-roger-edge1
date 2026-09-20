# Verifiche v0.3.0 — 20 settembre 2026

- ESPHome: **2026.9.0**.
- Destinazione: **ESP32 esp32dev, 4 MB, ESP-IDF**, configurazione privata originaria completa. Le misure di memoria sotto riportate si riferiscono a quella compilazione, non al nuovo esempio pubblico semplificato.
- Validazione configurazione: **PASS**.
- Compilazione C++ e link firmware: **PASS**; creati firmware OTA e factory nella cartella temporanea di collaudo.
- Flash applicazione: **1.250.175 byte / 1.835.008** (68,1%).
- RAM statica indicata dal build: **67.100 byte / 124.580** (53,9%; non è una misura della memoria libera durante l'esecuzione).
- Test motore protocollo: **PASS** con Clang, warning trattati come errori e UndefinedBehaviorSanitizer.
- Test revisione A+B: **PASS** con Clang, warning trattati come errori e UndefinedBehaviorSanitizer.
- Test FT1/FT2: **PASS** con Clang, warning trattati come errori e UndefinedBehaviorSanitizer.
- Test configurazione: **PASS**, 7 metodi / 23 scenari: configurazione completa,
  singole entità facoltative, YAML senza ingressi, schema vuoto rifiutato, timing,
  P38 opzionale e rifiuto di Modbus sulla stessa UART.

## Prove con acquisizioni reali

- **977 campioni di `0x1580`** dalle nove registrazioni: il filtro modifica solamente
  i tre campioni anomali (`0xF063` nel pedonale e i due `0x0F11` a inizio apertura).
  Gli altri campioni conservano la conversione prevista, compreso il fine pedonale
  `0xF965` con percentuali 40% e 0%.
- **26 richieste/ACK FC06**: corrispondenza byte per byte con i comandi e le scritture
  dei parametri generati dal componente.
- **4 varianti richiesta/risposta FC03 a blocchi**: P38=0/1 e P80=0/1; lettura dei
  valori agli offset osservati e gestione delle risposte complete da 25 byte.
- **33 coppie richiesta/risposta FC03 ingressi** dalle dieci acquisizioni nuove:
  richiesta identica byte per byte al blocco `0x1711`, quantità 2, risposta da 9 byte.
  Verificati parola ingressi, secondo registro separato e due maschere FT1/FT2.
- **Sequenza di 21 risposte app/web 2**: quattro letture consecutive FT1 attiva
  e cinque FT2 attiva, seguite da letture a zero. Il replay usa intervalli di
  polling regolari di test, non gli istanti fisici di ostruzione/rilascio.

Gli estratti sono in `tests/captured_fixtures.h`, rigenerabili con
`tests/import_captures.py` dalla cartella dell'analisi già effettuata. Non includono
i broadcast di configurazione. Il replay dei campioni di posizione mantiene i
tempi acquisiti, arrotondati ai millisecondi; non simula il campionamento ESPHome a
500 ms. La prima risposta di stato del simulatore di protocollo è sintetica,
perché il firmware legge un registro mentre B-CONNECT leggeva il blocco di sei.
Gli ingressi sono estratti in `tests/captured_inputs.h`, rigenerabili tramite
`tests/import_input_captures.py`. Le fixture non comprendono console, identificativi
dei dispositivi o scritture di servizio.

## Verifiche specifiche delle fotocellule

- ON/OFF da maschere indipendenti; casi sintetici per `0x0030`, bit estranei
  contemporaneamente attivi e seconda parola diversa da `0x0036`. Questi casi
  sintetici non sono presentati come acquisizioni effettuate sulla centrale.
- Nessun valore valido iniziale e nessun OFF artificiale dopo errori. Una risposta
  con il conteggio byte del vecchio registro eventi non viene accettata come ingressi.
- Scadenza esattamente a 3 s dall'ultima risposta valida mentre lo stato ante continua
  ad arrivare; una sola invalidazione per interruzione e ripresa dalla prima lettura valida.
- Caso inverso: ante senza risposta e ingressi ancora validi; indipendenza dei due
  timestamp. Timeout configurabile e overflow del contatore `millis()`.
- Risposte frammentate, rumore, CRC errato, vecchio frame parziale, eccezione e risposta
  tardiva; nessuno di questi casi pubblica “libero”.
- Ingressi ogni 500 ms nel simulatore con risposte rapide, anche mentre si leggono
  e scrivono i parametri; nessuna richiesta ingressi quando la funzione non è abilitata.
- STOP prioritario dopo risposta/timeout della richiesta pendente e cancellazione
  del movimento accodato. Con movimenti ripetuti, ante e ingressi continuano a essere letti.
- Con entrambe le letture scadute e timeout più lungo dell'intervallo, alternanza
  fra stato e ingressi senza privilegiare sempre lo stesso registro.

Il sorgente ESPHome 2026.9.0 installato conferma `BinarySensor::invalidate_state()`
e la trasmissione `missing_state` nell'API. La compilazione completa include le
chiamate di invalidazione e il codice generato configura le due entità FT1/FT2,
i sensori raw e gli intervalli 500/3000 ms. La consegna non è un test completo
dell'interfaccia Home Assistant collegata a hardware reale.

## Casi aggiuntivi

- Conferma di un vero salto estremo alla seconda lettura, eliminazione del candidato
  quando la posizione ritorna o assume un valore intermedio, reset dopo stato
  sconosciuto, scadenza della telemetria e rollover di `millis()`.
- Letture dei soli selettori abilitati, sincronizzazione iniziale/periodica/al
  ripristino della comunicazione, nessuna scrittura automatica all'avvio.
- ACK che non pubblica il parametro; rilettura che restituisce un valore diverso
  da quello scritto; ACK perso seguito da rilettura senza ripetizione della scrittura.
- Blocchi frammentati, CRC errato, conteggio byte errato, risposta parziale scaduta,
  valore fuori 0/1, eccezione e timeout. Le letture fallite non causano retry continui.
- Priorità STOP durante una lettura a blocchi, cancellazione del movimento in coda,
  polling dello stato conservato; una risposta parametro non rinnova la validità
  della telemetria delle ante.
- Restano superati i test precedenti: CRC noto, 65.536 parole di stato, comandi,
  code limitate, matching ACK, rumore, errori e gestione offline.

I test di build usavano credenziali sintetiche solo nella cartella temporanea.
Il pacchetto non include quei firmware né un secrets.yaml.
Durante i test di sviluppo non sono stati effettuati caricamenti o movimenti reali.
Successivamente l’autore ha installato la versione 0.3.0 e ne ha confermato il
funzionamento; le schermate Home Assistant mostravano FT1/FT2 in modo coerente
con i valori raw 16/32 e ritorno a 0, oltre alle ante chiuse con raw 15 e posizione 0%.
Non è documentata una prova fisica simultanea dei fasci: `0x0030` resta confermato
dai test software. Ulteriori prove sono in `COLLAUDO_FOTOCELLULE.md`.

## Preparazione del repository pubblico

Il codice in `components/roger_edge1` è identico byte per byte a quello della
versione 0.3.0 consegnata. Sono stati adattati documentazione, configurazione di
esempio e percorsi dei test. Il YAML pubblico usa DHCP, credenziali esterne e
nomi brevi per i pulsanti; non modifica la configurazione installata dall'autore.
Le fixture già presenti rendono i test eseguibili senza le registrazioni originali.
La rigenerazione delle fixture richiede invece le cartelle di analisi esterne,
che non fanno parte del repository.

Dopo la preparazione della copia pubblica sono stati rieseguiti con successo:

- i 7 metodi / 23 scenari della validazione YAML;
- tutte e tre le suite C++ (protocollo, revisione, ingressi);
- il confronto byte per byte degli 11 sorgenti del componente;
- il controllo dei link locali della documentazione e dei riferimenti alla configurazione privata da escludere.

La compilazione completa del firmware riportata all'inizio è quella già effettuata
sulla configurazione originaria; non viene presentata come una nuova compilazione
dell'esempio pubblico.
