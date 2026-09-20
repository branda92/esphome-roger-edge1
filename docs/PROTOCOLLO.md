# Protocollo UART implementato

Mappa ricavata dalle acquisizioni della centralina usata per lo sviluppo.

## Gestione della comunicazione

- Una sola richiesta sul filo alla volta. Ante e ingressi hanno un intervallo
  obiettivo di 500 ms ciascuno. Se entrambi sono da leggere, si alternano; una
  risposta lenta o mancante può allungare l'intervallo effettivo. I parametri usano
  blocchi di 10 registri, negli spazi disponibili tra telemetria e comandi.
- La coda contiene al massimo uno STOP, l'ultimo comando di movimento e l'ultima
  richiesta per ciascuno dei due parametri. Non cresce con i clic ripetuti.
- STOP precede le richieste ancora da inviare e cancella i movimenti accodati.
  Una richiesta già trasmessa termina con la risposta o il timeout prima dello STOP.
- Con il polling ingressi attivo, dopo un comando di movimento si lascia spazio
  a una lettura di telemetria dovuta prima di un altro movimento. I clic ripetuti
  non impediscono quindi di leggere ante e fotocellule. STOP mantiene la priorità.
- CRC, indirizzo, funzione, lunghezza ed eco registro/valore FC06 vengono controllati.
  Pacchetti spezzati, rumore, risposte estranee ed eccezioni non diventano stati validi.
- Una scrittura senza conferma **non viene ritrasmessa automaticamente**: un ACK perso
  non significa che la centrale non abbia già eseguito il comando. I comandi in coda
  scadono dopo 2 secondi. Il polling riprende autonomamente.
- Apri/chiudi/pedonale e parametri richiedono uno stato UART recente. STOP è accettato
  anche quando manca la telemetria. Nessun comando viene ripristinato al riavvio.
- Dopo `offline_timeout`, `EDGE1 Collegata` si spegne, i sensori numerici delle ante diventano
  `NaN`/sconosciuti e gli stati testuali diventano `Non disponibile`.
- Le fotocellule e i loro sensori raw scadono separatamente secondo `inputs_timeout`.

**Comportamento di questa versione:** cover e select conservano l'ultimo stato
pubblicato quando cade solo la UART. La cover usa `assumed_state`; per automazioni
che dipendono dalla posizione, controlla anche `EDGE1 Collegata`. Prima della prima
lettura valida il componente non pubblica uno stato della cover. In caso di posizione
sconosciuta mantiene l'ultima posizione della cover, mentre i sensori percentuali
diventano sconosciuti.

Il master Modbus è implementato direttamente sulla UART per mantenere un'unica coda
e controllare scadenza e priorità dei comandi. **Non aggiungere `modbus` o
`modbus_controller` sulla stessa UART**: la validazione del componente rifiuta questa
configurazione. Anche un `uart.debug` con `dummy_receiver: true` è incompatibile.
Mantieni il cablaggio già collaudato, con un solo master sulla porta EXP; il B-CONNECT
originale non deve trasmettere contemporaneamente. Il componente presuppone RX senza
eco locale di TX: per FC06 un'eco elettrica è indistinguibile dall'ACK della centrale.

## Mappa implementata

| Operazione | Funzione Modbus | Registro | Valore / significato |
| --- | --- | --- | --- |
| Stato | FC03 | `0x1580` | Un registro, ordine dei nibble `P2 P1 S2 S1` |
| Fotocellule | FC03 | `0x1711`, quantità 2 | FT1 = bit 4, FT2 = bit 5 della prima parola; seconda parola solo raw |
| Stop | FC06 | `0x1965` | `0x6801` |
| Apri | FC06 | `0x1965` | `0x6802` |
| Chiudi | FC06 | `0x1965` | `0x6804` |
| Pedonale | FC06 | `0x1965` | `0x6810` |
| Parametro 38 | FC06 | `0x1603` | `0x2600` / `0x2601` |
| Parametro 80 | FC06 | `0x1622` | `0x5000` / `0x5001` |
| Lettura parametro 38 | FC03 | Blocco `0x15FE`, 10 registri | Indice 5 contando da zero = `0x1603`, valore semplice 0/1 |
| Lettura parametro 80 | FC03 | Blocco `0x161C`, 10 registri | Indice 6 contando da zero = `0x1622`, valore semplice 0/1 |

Indirizzo predefinito `0x0A`; UART `115200 8N1`. Posizione raw: 0 = aperta, 15 = chiusa.
Percentuale: `(15 - raw) * 100 / 15`.

Gli stati 1–6 hanno posizione utilizzabile. Gli stati 0, 7–15 vengono conservati come
codice/testo ma non convertiti in una percentuale attendibile. In particolare lo stato
12, ambiguo nella mappa disponibile, non viene equiparato ad apertura completa.
Le prime acquisizioni associavano `0x0400`/`0x0800` a eventi FT1/FT2 in `0x1582`.
Le nuove acquisizioni mostrano anche altri codici transitori sullo stesso registro.
La lettura persistente verificata è invece `0x1711`: per questo la versione 0.3.0
usa quel registro. Altri ingressi, statistiche e allarmi non sono implementati.


## Filtro della posizione

Ogni anta applica un filtro ai soli salti estremi raw 15↔0: la seconda lettura
consecutiva conferma il nuovo estremo. Durante la prima lettura conserva l'ultima
percentuale; i codici di stato e i sensori raw si aggiornano subito. I movimenti
ordinari non vengono rallentati dal filtro. Con polling a 500 ms la conferma
richiede normalmente un intervallo aggiuntivo.

Il filtro viene azzerato quando la posizione è sconosciuta o la telemetria scade.
Il campione di fine pedonale osservato `0xF965` rimane Anta 1 al 40%, Anta 2 allo
0%, media 20%; non viene etichettato automaticamente come apertura completa.

## Vincoli dei tempi

- `offline_timeout` deve superare `update_interval + response_timeout`.
- `parameter_update_interval`: da 1 secondo a 24 ore.
- `inputs_update_interval`: da 100 ms a 10 secondi.
- `inputs_timeout`: al massimo 60 secondi e maggiore di `inputs_update_interval + response_timeout`.

Una UART dedicata per ciascun hub; una sola cover e una sola entità per ogni tipo
di sensore o parametro sullo stesso hub.
