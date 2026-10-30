125A Emulator III - D0 Test ohne echte Diskette

1. Das gesamte Windows-QA-Artifact ZIP entpacken.
2. Start.cmd doppelklicken.
3. Im DOS-Fenster erscheinen die Tests und PASS/FAIL.
4. D0-Console.txt fuer die technische Auswertung bereitstellen.

Das Testprogramm erzeugt im Windows-Temp-Ordner synthetische 800-KB-IMG-
und HFE-v1-Abbilder. Diese Dateien stammen NICHT von einer originalen EIII-
Diskette; sie enthalten KEINE proprietaeren Samples und werden nach dem
Test entfernt. Original-ROM, Festplatten-OS oder echte Disketten sind nicht
notwendig.

Pruefungen: WD1772 Register, Sektorbytes/CRC, Schreibschutz, Index/Flux,
IMG/HFE-Fehlerbehandlung, Medien-Fingerabdruck, Controller-/PLL-Zustand
waehrend eines laufenden Sektorlesens, versionierter D0-Snapshot,
Checksumme und deterministisches Fortsetzen ab dem gespeicherten Byte.

Dieser Konsolentest kann allein keinen kompletten Studio-One-Projekt-Recall
oder einen Firmware-Bank-Load beweisen. Diese Praxistests folgen separat.
