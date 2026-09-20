# Versioni

## 0.3.0 — 20 settembre 2026

- FT1/FT2 dal primo registro del blocco FC03 `0x1711`, quantità 2: ON = fascio
  oscurato, usando rispettivamente le maschere `0x0010` e `0x0020`.
- Polling ingressi configurabile, 500 ms per default, attivato dalle sole entità
  configurate; validità separata dal registro ante, con scadenza a 3 s per default.
- Stato iniziale sconosciuto, invalidazione nativa dei binary sensor in assenza
  di letture recenti e ripristino alla prima risposta valida. Nessun falso OFF
  dovuto a CRC, eccezione, timeout o riavvio.
- Diagnostica opzionale `inputs_raw` e `input_aux_raw`; secondo registro senza
  interpretazione. Schema compatibile con la configurazione precedente.
- Letture ante/ingressi alternate quando entrambe sono dovute, anche con risposte
  lente; telemetria servita fra movimenti ripetuti, STOP sempre prioritario.
- Test con 33 risposte catturate e casi sintetici per entrambi i fasci/bit aggiuntivi,
  scadenza indipendente, recupero, framing, STOP e integrazione con i parametri.

## 0.2.0 — 18 settembre 2026

- Filtro per anta sui salti di posizione raw 15↔0: richiede una seconda lettura
  consecutiva, conserva raw e stato di movimento, si azzera su posizione sconosciuta
  o telemetria scaduta. Riproducendo i 977 campioni acquisiti modifica solo i tre
  campioni anomali individuati nell'analisi.
- Letture effettive di P38/P80 tramite i blocchi FC03 catturati, all'avvio,
  alla riconnessione, dopo scrittura e periodicamente (default 30 s).
- Select aggiornati dalla lettura, non dall'ACK. Lettura di verifica anche con ACK
  perso; nessuna ritrasmissione automatica delle scritture.
- Parser esteso alle risposte da 25 byte e verifica dei valori 0/1, con coda unica,
  priorità STOP e polling dello stato conservati.
- Test con dati acquisiti e piano per nuove registrazioni FT1/FT2. Nessun sensore
  di stato fotocellula aggiunto: occorre identificare il comportamento del fascio.

## 0.1.0 — 17 settembre 2026

Prima versione: stato delle ante, percentuali, cover, apri/stop/chiudi/pedonale,
scrittura P38/P80, contatori diagnostici e gestione della connessione UART.
