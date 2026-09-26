# sommacampagna external summing - Windows release

## Contenuto

- `sommacampagna_engine.exe`
- `plugins/sommacampagna_sender.vst3`
- `plugins/sommacampagna_receiver.vst3`

## Installazione plugin

Richiede Windows 10/11 x64 e Microsoft Visual C++ Redistributable 2015-2022 x64.

1. Chiudi la DAW.
2. Copia queste due cartelle in:

   `C:\Program Files\Common Files\VST3\`

   - `sommacampagna_sender.vst3`
   - `sommacampagna_receiver.vst3`

3. Apri la DAW e fai una scansione VST3.

## Utilizzo rapido

1. Avvia `sommacampagna_engine.exe`.
2. Inserisci `sommacampagna_sender` sulle tracce da inviare.
3. Inserisci `sommacampagna_receiver` su una traccia return/aux.
4. Premi play.

## Note

- Il sender muta il segnale post-invio (niente doppio audio).
- Engine, sender e receiver si sincronizzano via file porte UDP condiviso.
- File porte: `%APPDATA%\sommacampagna\udp-ports.txt`
- Engine, sender, receiver e DAW devono usare lo stesso sample rate.
- Il percorso esterno aggiunge latenza e non comunica ancora la latenza alla DAW per la compensazione automatica.
- Le preview non firmate possono mostrare avvisi SmartScreen. Verifica sempre checksum e provenienza dalla GitHub Release ufficiale.
