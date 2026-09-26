# sommacampagna external summing - macOS Universal

## Contenuto

- `Applications/sommacampagna_engine.app`
- `Plugins/VST3/sommacampagna_sender.vst3`
- `Plugins/VST3/sommacampagna_receiver.vst3`
- `Plugins/Components/sommacampagna_sender.component`
- `Plugins/Components/sommacampagna_receiver.component`

Tutti i binari includono le architetture `arm64` e `x86_64` e richiedono macOS 12 o successivo.

## Installazione consigliata

Scarica dalla stessa GitHub Release anche `SHA256SUMS.txt`, poi verifica lo ZIP prima di estrarlo:

```bash
grep 'macos-universal.zip$' SHA256SUMS.txt | shasum -a 256 -c -
```

Apri Terminale nella cartella estratta ed esegui:

```bash
bash install-macos.command
```

Chiudi prima tutte le DAW. Lo script mostra le destinazioni, chiede conferma e installa tutto per l'utente corrente senza `sudo`. Rimuove la quarantena soltanto dai cinque bundle installati e prova a validare i due Audio Unit.

## Installazione manuale

1. Chiudi la DAW.
2. Copia i VST3 in `~/Library/Audio/Plug-Ins/VST3/` oppure `/Library/Audio/Plug-Ins/VST3/`.
3. Copia gli AU in `~/Library/Audio/Plug-Ins/Components/` oppure `/Library/Audio/Plug-Ins/Components/`.
4. Copia `sommacampagna_engine.app` in `/Applications` o in una cartella applicazioni dell'utente.
5. Apri la DAW e ripeti la scansione dei plugin.

Per una build scaricata e verificata dalla release ufficiale, rimuovi la quarantena esclusivamente dai bundle installati con `xattr -dr com.apple.quarantine <percorso-bundle>`.

## Disinstallazione

Esegui dalla cartella estratta:

```bash
bash uninstall-macos.command
```

Per rimuovere anche il file di discovery/configurazione usa `bash uninstall-macos.command --purge`.

## Utilizzo rapido

1. Avvia `sommacampagna_engine.app`.
2. Inserisci `sommacampagna_sender` sulle tracce da inviare.
3. Inserisci `sommacampagna_receiver` su una traccia return/aux.
4. Premi play.

## Note

- Il sender muta il segnale post-invio per evitare il doppio audio.
- Engine, sender, receiver e DAW devono usare lo stesso sample rate.
- Il percorso esterno aggiunge latenza e non comunica ancora la latenza alla DAW per la compensazione automatica.
- File porte: `~/Library/sommacampagna/udp-ports.txt`.
- Le preview non firmate o non notarizzate possono essere bloccate da Gatekeeper. Non rimuovere la quarantena su file di provenienza non verificata.
