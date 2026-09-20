# Roger EDGE1 per ESPHome

Componente esterno ESPHome per integrare una centralina **Roger Technology EDGE1** in Home Assistant attraverso la porta UART EXP.

Gestisce apertura, chiusura, stop, apertura pedonale, stato e posizione delle due ante e fotocellule FT1/FT2.

**Versione del componente: 0.3.0.** Compilazione verificata con **ESPHome 2026.9.0**, **ESP32 esp32dev, 4 MB**, framework **ESP-IDF**. Il funzionamento sulla centralina dell'autore è stato confermato; le verifiche documentate delle fotocellule mostrano FT1 e FT2 separatamente.

Questo è un progetto indipendente, non un componente ufficiale Roger Technology o ESPHome. Il protocollo implementato deriva dalle acquisizioni UART dell'impianto utilizzato per lo sviluppo: non è stata verificata la compatibilità con tutte le centraline o revisioni firmware.

## Installazione da GitHub

Aggiungi al tuo YAML ESPHome:

```yaml
external_components:
  - source: github://branda92/esphome-roger-edge1@main
    components: [roger_edge1]
    refresh: 1d
```

L'esempio completo è in **[examples/esp32-gate.yaml](examples/esp32-gate.yaml)**. Usa DHCP e mantiene le credenziali in `secrets.yaml`:

1. Copia `examples/esp32-gate.yaml` nella cartella di configurazione ESPHome.
2. Usa `examples/secrets.example.yaml` come riferimento per le chiavi da inserire nel tuo `secrets.yaml` esistente, oppure creane uno accanto al YAML. I valori dell'esempio sono segnaposto e vanno sostituiti.
3. Verifica modello ESP32, GPIO e interfaccia elettrica in base al tuo impianto.
4. Valida e compila con ESPHome Device Builder, poi installa tramite il normale flusso ESPHome.

Da riga di comando:

```sh
esphome config esp32-gate.yaml
esphome compile esp32-gate.yaml
```

Il riferimento `main` segue il ramo principale. Per bloccare la configurazione a una versione, sostituiscilo con un tag effettivamente disponibile nel repository. L'aggiornamento del codice sorgente avviene durante la preparazione della compilazione; il firmware sul dispositivo cambia solo dopo l'installazione.

### Installazione locale

Copia `components/roger_edge1` dentro una cartella `components` accanto al YAML e sostituisci la sorgente GitHub con:

```yaml
external_components:
  - source:
      type: local
      path: components
    components: [roger_edge1]
```

## Collegamento UART

La configurazione collaudata usa **115200 baud, 8 bit, nessuna parità, 1 stop bit**, indirizzo Modbus **`0x0A`**:

```yaml
uart:
  id: uart_roger
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 115200
  data_bits: 8
  parity: NONE
  stop_bits: 1

roger_edge1:
  id: edge1
  uart_id: uart_roger
  address: 0x0A
  update_interval: 500ms
  response_timeout: 200ms
  offline_timeout: 3s
  parameter_update_interval: 30s
  inputs_update_interval: 500ms
  inputs_timeout: 3s
```

I GPIO sono quelli dell'esempio ESP32 e non costituiscono una piedinatura universale della porta EXP. Questo repository non documenta una piedinatura elettrica valida per ogni revisione della scheda. Usa un'interfaccia e collegamenti verificati per la tua centralina.

Il componente gestisce direttamente la UART: **un solo master deve trasmettere sulla porta EXP**. B-CONNECT non deve trasmettere contemporaneamente all'ESP32. Non configurare `modbus`, `modbus_controller` o un altro ricevitore sulla stessa UART. È richiesta una ricezione senza eco locale di TX, perché in FC06 l'eco della richiesta è indistinguibile dalla conferma della centrale.

## Entità disponibili

| Entità | Funzione |
|---|---|
| Cover Cancello | Apri, stop, chiudi; stato aggregato delle due ante |
| Pulsanti Apri / Stop / Chiudi / Pedonale | Comandi singoli alla centrale |
| Posizione Anta 1 / 2 | Percentuale di apertura, filtrata sui salti estremi |
| Posizione Cancello | Media delle percentuali delle due ante |
| Stato Anta 1 / 2 | Stato testuale di ciascuna anta |
| Codice Stato Anta 1 / 2 | Codice numerico diagnostico |
| EDGE1 Collegata | Lettura dello stato ante ricevuta entro `offline_timeout` |
| FT1 Oscurata / FT2 Oscurata | Stato dei due fasci, con scadenza indipendente dalla telemetria delle ante |
| EDGE1 Stato Grezzo / posizioni raw | Valori originali letti dalla centralina |
| EDGE1 Ingressi Raw 0x1711 | Parola completa degli ingressi |
| EDGE1 Registro Raw 0x1712 | Seconda parola, senza interpretazione; disabilitata per default nell'esempio |
| Errori comunicazione / CRC | Contatori diagnostici dall'ultimo riavvio |
| Ultimo esito comando | Accodamento, conferma della scrittura oppure errore |
| Parametro 80 | Selettore 0/1, aggiornato dal valore effettivamente riletto |
| Parametro 38 | Stessa gestione; esempio facoltativo commentato nel YAML |

Non è disponibile un comando di destinazione percentuale: la cover non espone un cursore e il componente non simula arresti mediante timer. La percentuale aggregata non rappresenta la larghezza utile del passaggio.

I nomi dei pulsanti nell'esempio sono brevi (`Apri`, `Stop`, `Chiudi`, `Pedonale`). Home Assistant gestisce autonomamente i relativi ID; `id:` nel YAML ESPHome serve ai riferimenti interni. Per aggiornare un'installazione esistente senza rinominare entità, conserva il tuo YAML e aggiorna soltanto la sorgente del componente.

## Fotocellule FT1 e FT2

La lettura usa FC03 dal registro **`0x1711`**, quantità **2 registri**. Il primo contiene due bit indipendenti:

| Valore dei bit FT | FT1 | FT2 |
|---|---|---|
| `0x0000` | Libera | Libera |
| `0x0010` | Oscurata | Libera |
| `0x0020` | Libera | Oscurata |
| `0x0030` | Oscurata | Oscurata |

Il sensore raw mostra normalmente numeri decimali: **16**, **32** e **48** corrispondono ai tre valori non nulli della tabella. In presenza di altri ingressi, considera `raw & 0x0030`.

```yaml
binary_sensor:
  - platform: roger_edge1
    roger_edge1_id: edge1
    online:
      name: EDGE1 Collegata
    ft1:
      name: FT1 Oscurata
    ft2:
      name: FT2 Oscurata
```

`ON` significa fascio oscurato e `OFF` libero. All'avvio e dopo `inputs_timeout` senza risposte valide, lo stato è **sconosciuto**, senza un falso OFF. I sensori raw diventano anch'essi sconosciuti. La prima nuova lettura valida ripristina gli stati.

CRC errato, eccezione o timeout non significano fascio libero. La freschezza delle fotocellule è indipendente da `EDGE1 Collegata`: le ante possono rispondere mentre gli ingressi non rispondono, o viceversa.

Il secondo registro, `0x1712`, è soltanto diagnostico. Il registro eventi `0x1582` non viene usato per ricostruire il fascio. Il componente legge le fotocellule senza impartire comandi automatici al cancello in base al loro stato.

Procedura completa: **[COLLAUDO_FOTOCELLULE.md](COLLAUDO_FOTOCELLULE.md)**.

## Parametri e comportamento della comunicazione

I parametri 38 e 80 sono supportati esclusivamente per valori **0/1**, con lettura all'avvio dopo il primo stato valido, alla riconnessione, dopo una scrittura e ogni 30 secondi per default. Sono letti soltanto quelli configurati. Non vengono scritti valori automaticamente all'avvio.

Il selettore cambia stato dalla rilettura, non dalla sola conferma FC06. Un ACK perso comporta una rilettura senza ripetere la scrittura. In caso di errore il selettore conserva l'ultimo valore letto. Il repository non attribuisce ai parametri funzioni operative valide per tutte le configurazioni della centrale.

La coda è limitata e una sola richiesta può essere in attesa di risposta. Stop ha priorità sulle richieste ancora da trasmettere e cancella i movimenti accodati. Una richiesta già trasmessa termina con la risposta o il timeout prima dell'invio di Stop. Le scritture non sono ritentate automaticamente: la mancanza di ACK non esclude che la centrale le abbia eseguite.

Apri, chiudi, pedonale e scrittura parametri richiedono una lettura recente dello stato ante. Stop resta accettato in assenza di telemetria. Quando cade la sola UART, **cover e selettori conservano l'ultimo stato pubblicato**: per valutare la posizione nelle automazioni considera anche `EDGE1 Collegata`. I sensori numerici e testuali delle ante, invece, diventano sconosciuti/non disponibili.

Dettagli su registri, filtro posizione, tempi e coda: **[docs/PROTOCOLLO.md](docs/PROTOCOLLO.md)**.

## Sviluppo e test

Servono Python **3.12–3.14** e un compilatore C++17. Dalla radice del repository:

```sh
python3 -m venv .venv
.venv/bin/pip install -r requirements-dev.txt
.venv/bin/python tests/test_config.py -v
python3 tests/run_protocol_tests.py
```

I test YAML usano una copia temporanea dell'esempio con componenti locali e credenziali sintetiche: non scaricano il ramo remoto e non si collegano al cancello. I test C++ usano lo stesso motore di protocollo del firmware, con UndefinedBehaviorSanitizer e warning trattati come errori.

Sono inclusi estratti delle acquisizioni per riprodurre 977 campioni di stato, 26 scritture/ACK, 4 varianti di lettura dei parametri e 33 risposte degli ingressi. Le registrazioni originali `.sr`, i dati della rete domestica e i firmware compilati non sono inclusi.

- [Risultati e limiti delle verifiche](TEST_RESULTS.md)
- [Cronologia delle versioni](CHANGELOG.md)
- [Componenti esterni ESPHome](https://esphome.io/components/external_components/)
- [Architettura UART ESPHome](https://developers.esphome.io/architecture/components/uart/)

## Licenza

Licenza MIT: [LICENSE](LICENSE).
