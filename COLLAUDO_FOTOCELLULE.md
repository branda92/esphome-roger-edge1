# Collaudo FT1/FT2 — versione 0.3.0

La versione 0.3.0 è stata installata dall’autore, che ne ha confermato il funzionamento.
Le osservazioni disponibili verificano FT1 e FT2 separatamente a cancello chiuso.
Le prove seguenti servono a completare il collaudo su ciascuna installazione.

## Configurazione

Usa l'esempio `examples/esp32-gate.yaml` o aggiungi al tuo YAML le entità FT1/FT2
come descritto nel README. Conserva nomi e ID della configurazione esistente se
vuoi mantenere le associazioni di Home Assistant. Compila e installa tramite
ESPHome. Mantieni un solo master sulla porta EXP.

## Prova a cancello fermo

Confronta i sensori con la situazione fisica. `ON` significa **oscurata**;
`OFF` significa **libera**. Lo stato sconosciuto significa che manca una lettura recente.

| Prova | FT1 attesa | FT2 attesa | `0x1711` atteso limitatamente ai bit FT |
| --- | --- | --- | --- |
| Entrambi i fasci liberi | OFF | OFF | `0x0000` |
| FT1 oscurata per 10 s | ON per tutta l'ostruzione | OFF | `0x0010` |
| FT1 nuovamente libera | OFF | OFF | `0x0000` |
| FT2 oscurata per 10 s | OFF | ON per tutta l'ostruzione | `0x0020` |
| Entrambe oscurate | ON | ON | `0x0030` |
| Libera solo FT1, mantenendo FT2 oscurata | OFF | ON | `0x0020` |
| Libera anche FT2 | OFF | OFF | `0x0000` |

Se sono attivi altri ingressi, la parola raw può contenere ulteriori bit: confronta
`raw & 0x0030` con l'ultima colonna. Il sensore raw numerico mostra normalmente il
valore decimale: 16 = `0x0010`, 32 = `0x0020`, 48 = `0x0030`.

Lascia qualche secondo tra una prova e l'altra. Il polling obiettivo è 500 ms;
una richiesta pendente, un timeout o il trasporto verso Home Assistant possono
allungare il tempo visualizzato. Non viene applicato un impulso temporizzato al
segnale: un valore attivo resta tale finché le letture valide continuano a riportarlo.

La prova simultanea `0x0030` è nuova: il comportamento è testato in simulazione,
ma questa combinazione non compariva nelle acquisizioni originali.

## Avvio e perdita di comunicazione

- Riavvia soltanto l'ESP32, una volta con fasci liberi e una volta con FT1 già
  oscurata. Il componente non pubblica un OFF iniziale assunto: la prima risposta
  valida deve produrre il valore reale.
- Se verifichi l'assenza di telemetria, osserva FT1/FT2 dopo almeno tre secondi:
  devono diventare sconosciute, senza una falsa transizione a libero. Alla ripresa
  devono aggiornarsi dalla prima risposta valida.
- Non serve interrompere il cablaggio delle fotocellule o modificare le protezioni
  per questa verifica. Una perdita di sole letture `0x1711`, mentre `0x1580`
  continua a rispondere, è già coperta dai test software: `EDGE1 Collegata` può
  restare ON e le fotocellule devono comunque scadere.
- `EDGE1 Registro Raw 0x1712`, se abilitato da Home Assistant, è solo diagnostico.
  Il valore `0x0036` osservato nelle catture non rappresenta due fasci oscurati.

## Se il comportamento differisce

Conserva il log ESPHome e, se possibile, una registrazione `.sr` delle due direzioni
UART durante la prova. Annota fotocellula, istanti di ostruzione/rilascio e valore
visualizzato. Con l'analizzatore puoi osservare l'ESP32 come unico master; non è
necessario ricollegare B-CONNECT mentre ESPHome trasmette.

Le acquisizioni precedenti verificavano le fotocellule con ante chiuse. Il
comportamento durante il movimento resta una successiva prova della centrale.
Il componente legge questi segnali senza usarli per impartire comandi automatici.
