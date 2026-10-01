# Changelog

## 0.3.0

- Aggiunta la posizione assoluta in campioni della DAW a ogni pacchetto audio (protocollo v2) e la somma usa un unico cursore temporale condiviso tra tutti i sender.
- Rimossa la correzione di clock che variava la velocità e la lettura interpolata del receiver: ogni frame viene consumato una sola volta, senza resampling o time-stretch.
- Il motore processa blocchi in base ai frame effettivamente ricevuti, non a un timer indipendente; i buffer stabiliscono la latenza e non il BPM.
- I frame mancanti restano silenzio nella loro posizione temporale e le discontinuità sono esposte nella diagnostica, senza riallineare o accelerare stream singoli.
- I sender richiedono al plug-in host la posizione in campioni; i pacchetti senza una posizione valida vengono rifiutati per non sommare tracce non sincronizzate.
- Aggiunti test su payload PCM esatto, posizioni DAW condivise e mantenimento dei gap senza spostare i frame successivi.

## 0.2.0

- Sostituito il buffer per-stage con un unico target totale, ripartito tra engine e receiver e compatibile con i file discovery precedenti.
- Aggiunti rilevamento delle discontinuita sender, re-priming per stream e gestione esplicita degli overflow.
- Aggiunta telemetria locale per code, drop, underrun, resync, occupazione e correzione del clock.
- Aggiunti test deterministici del protocollo, della configurazione, delle sequenze e del buffer policy in CTest/CI.
- Rafforzato il recovery dopo restart di sender ed engine mantenendo invariato il protocollo wire v1.

## 0.1.1

- Aggiunti installer e uninstaller macOS per installazione locale all'utente senza `sudo`.
- L'installer rimuove la quarantena esclusivamente dai bundle installati e avvia la validazione AU.

## 0.1.0

- Prima preview del motore di somma esterno localhost.
- Sender e receiver VST3 per Windows x64 e macOS Universal.
- Sender e receiver AU per macOS Universal.
- Discovery dinamico delle porte UDP e controllo globale del buffer di trasmissione.
- Compensazione limitata del clock drift e re-sincronizzazione del receiver.
