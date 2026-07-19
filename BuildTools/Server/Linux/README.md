# Exploitation Linux du serveur dédié PropHunt

Ce dossier complète le package Linux cuisiné. Il ne contient aucun secret, ne modifie pas automatiquement le
pare-feu et n'effectue aucun déploiement distant.

Gate propriétaire : ne lancer aucune connexion SSH, copie distante, modification de pare-feu ou installation
systemd sur le VPS sans prévenir Thomas et attendre ses explications puis son accord explicite.

Deux modèles sont conservés sans ambiguïté :

- le mode **standalone Phase 2A** sous `/opt/prophunt`, réservé aux qualifications historiques P6/P7 ;
- le mode **release NOVA P9** sous `/home/ue-game/releases/<release>`, lance exclusivement par le worker Nova.

Etat courant au 2026-07-19 : NOVA selectionne uniquement `build-prophunt-p9-20260719-staging1`; le standalone P6
est arrete et ne doit pas etre reactive. Les anciennes releases ne sont pas routables.

Un bundle protocole `8+` exige `-NovaReleaseName`, exclut l'unité systemd et le lanceur standalone de son archive
ops, et ne doit jamais être installé ou démarré selon la procédure `/opt/prophunt` ci-dessous.

## Arborescence standalone Phase 2A historique

- package Unreal : `/opt/prophunt/server` ;
- scripts de ce dossier : `/opt/prophunt/ops` ;
- configuration locale : `/etc/prophunt/prophunt-server.env` ;
- état et HOME du compte : `/var/lib/prophunt` ;
- logs : `/var/log/prophunt` ;
- runtime SteamCMD 64 bits : `/opt/steamcmd/linux64/steamclient.so`.

Le service standalone s'exécute sous le compte non privilégié `prophunt`. Cette arborescence ne s'applique pas
au candidat P8/NOVA.

## Génération du bundle depuis Windows

Depuis la racine canonique du projet :

```powershell
& '.\BuildTools\Server\New-PropHuntVpsBundle.ps1' `
  -PackageRoot '.\package\server\LinuxServer' `
  -OutputDirectory '.\package\vps-bundle' `
  -ProtocolVersion 91 `
  -BuildVersion '0.1.1907002' `
  -NovaReleaseName 'build-prophunt-v0.1.1907002'
```

Le générateur refuse tout chemin extérieur au dépôt et tout écrasement. Par défaut, il lit `ProtocolVersion`
dans `Config/DefaultGame.ini`, exclut uniquement les symboles séparés `PropHuntServer.debug`/`.sym`, puis crée
les archives serveur/ops, un manifeste versionné et `SHA256SUMS`. Utiliser `-IncludeDebugFiles` seulement pour
produire volontairement une archive de diagnostic. `-ProtocolVersion` permet une surcharge explicite si un
paquet historique doit être reproduit. Le manifeste P8 fixe le modèle `nova-release`, les chemins de release et
le runtime Steam Nova `/home/ue-game/steamcmd/linux64/steamclient.so`. `SHA256SUMS` est écrit en LF pour être
directement vérifiable par `sha256sum -c` sur Debian.

## Audit initial du VPS en lecture seule

`audit-prophunt-vps-readonly.sh` collecte sur stdout la distribution, la capacité, les services et processus
ciblés, les conteneurs, les ports, le pare-feu visible, SteamCMD, les chemins PropHunt et des indicateurs de
sauvegarde. Il n'utilise pas `sudo`, ne crée aucun fichier et ne change aucun service, port ou pare-feu. Les
arguments complets des processus et les variables d'environnement ne sont volontairement pas affichés.

La commande suivante est préparée pour PowerShell. Elle diffuse le script par l'entrée standard de SSH : aucun
fichier d'audit n'est copié sur le VPS. Le rapport brut reste local sous `Saved/Diagnostics` et ne doit pas être
versionné sans relecture et suppression des informations d'infrastructure sensibles.

```powershell
$projectRoot = 'C:\Users\Thomas\Documents\Unreal Projects\PropHunt'
$auditScript = Join-Path $projectRoot 'BuildTools\Server\Linux\audit-prophunt-vps-readonly.sh'
$auditDirectory = Join-Path $projectRoot 'Saved\Diagnostics'
$auditOutput = Join-Path $auditDirectory ("Phase2A-VpsReadonly-{0}.txt" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $auditDirectory -Force | Out-Null
$auditPayload = [System.IO.File]::ReadAllText($auditScript).Replace("`r`n", "`n").Replace("`r", "")
# Le commentaire final absorbe le CRLF ajouté par le pipeline PowerShell vers un processus natif.
$auditPayload += "`n# powershell-pipeline-terminator`n"
$auditPayload |
  & ssh vps 'bash -s --' |
  Tee-Object -FilePath $auditOutput
if ($LASTEXITCODE -ne 0) { throw "Audit SSH interrompu avec le code $LASTEXITCODE" }
```

Cette commande reste soumise à la gate propriétaire : elle ne doit être lancée qu'après explication de l'état du
VPS et accord explicite. Les sections `[LIMITATION]` doivent être analysées avant d'envisager une seconde lecture
avec des droits supplémentaires ; elles ne justifient jamais une élévation automatique.

## Préparation manuelle sur le VPS

Cette section est une procédure préparée, pas une autorisation de l'exécuter. Obtenir d'abord l'accord explicite
du propriétaire conformément à la gate ci-dessus.

Après transfert du package Linux dans `/opt/prophunt/server` et de ce dossier dans `/opt/prophunt/ops` :

```bash
sudo useradd --system --home-dir /var/lib/prophunt --create-home --shell /usr/sbin/nologin prophunt
sudo install -d -o root -g prophunt -m 0750 /etc/prophunt
sudo install -d -o prophunt -g prophunt -m 0750 /var/lib/prophunt /var/log/prophunt
sudo chown -R root:prophunt /opt/prophunt/server /opt/prophunt/ops
sudo find /opt/prophunt/server /opt/prophunt/ops -type d -exec chmod 0750 {} +
sudo find /opt/prophunt/server /opt/prophunt/ops -type f -exec chmod 0640 {} +
sudo chmod 0750 /opt/prophunt/server/PropHuntServer.sh
if [ -f /opt/prophunt/server/PropHunt/Binaries/Linux/PropHuntServer-Linux-Shipping ]; then
  sudo chmod 0750 /opt/prophunt/server/PropHunt/Binaries/Linux/PropHuntServer-Linux-Shipping
else
  sudo chmod 0750 /opt/prophunt/server/PropHunt/Binaries/Linux/PropHuntServer
fi
sudo chmod 0750 /opt/prophunt/ops/run-prophunt-server.sh /opt/prophunt/ops/preflight-prophunt-server.sh \
  /opt/prophunt/ops/test-vps-bundle-local.sh
if [ -f /opt/prophunt/ops/audit-prophunt-vps-readonly.sh ]; then
  sudo chmod 0750 /opt/prophunt/ops/audit-prophunt-vps-readonly.sh
fi
sudo cp /opt/prophunt/ops/prophunt-server.env.example /etc/prophunt/prophunt-server.env
sudo chown root:prophunt /etc/prophunt/prophunt-server.env
sudo chmod 0640 /etc/prophunt/prophunt-server.env
```

SteamCMD doit fournir `/opt/steamcmd/linux64/steamclient.so`. Le lanceur ajoute ce dossier à
`LD_LIBRARY_PATH`; aucune bibliothèque Steam provenant d'un autre projet n'est copiée dans PropHunt.

La normalisation `chmod` ci-dessus est obligatoire : une archive créée depuis Windows ne conserve pas les bits
exécutables Linux. Le pré-vol refuse volontairement de démarrer tant que cette étape n'a pas été appliquée.

Avant tout contact avec le VPS, le bundle peut être retesté entièrement sous WSL :

```bash
bash /chemin/vers/test-vps-bundle-local.sh \
  /chemin/vers/PropHuntServer-Linux-Protocol7-Runtime.tar.gz \
  /chemin/vers/PropHuntServer-Ops-Protocol7.tar.gz
```

Ce test utilise un répertoire `mktemp` borné sous `/tmp`, normalise les modes et exécute le pré-vol. Pour P8
Shipping il ne fabrique pas de faux runtime `-NoSteam` : le serveur refuse désormais de démarrer sans identité
NOVA, roster exact et Steam. Le smoke P8 réel doit donc passer par Nova et son sidecar dans une fenêtre VPS
explicitement autorisée.

## Pré-vol et service standalone Phase 2A uniquement

Valider d'abord le package seul :

```bash
/opt/prophunt/ops/preflight-prophunt-server.sh --package /opt/prophunt/server --package-only
```

Puis valider l'hôte complet :

```bash
sudo -u prophunt /opt/prophunt/ops/preflight-prophunt-server.sh \
  --env /etc/prophunt/prophunt-server.env
```

Après réussite du pré-vol :

```bash
sudo cp /opt/prophunt/ops/prophunt-server.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now prophunt-server.service
sudo systemctl status prophunt-server.service
journalctl -u prophunt-server.service -f
```

Le pare-feu doit autoriser en entrée uniquement les ports UDP choisis, par défaut `7777/udp` pour le jeu et
`27015/udp` pour la query Steam. Exemple UFW à exécuter seulement après confirmation de l'interface et de la
politique réseau du VPS :

```bash
sudo ufw allow 7777/udp comment 'PropHunt game'
sudo ufw allow 27015/udp comment 'PropHunt Steam query'
```

## Critères de validation réelle

Le service n'est considéré fonctionnel qu'après preuve simultanée de :

1. `SteamSocketsNetDriver ... started listening` dans les logs ;
2. `Gameserver logged on to Steam` puis `AuthStatus ... OK` ;
3. `Serveur dédié publié et prêt à recevoir les clients` sans overflow de tag ;
4. découverte et jonction depuis un second compte Steam du même protocole ;
5. joueurs `Unassigned` pendant la fenêtre, puis exactement un Tueur choisi côté serveur sans dépendre de
   l'ordre de connexion et passage en Traque avec le roster attendu ;
6. déconnexion, arrêt et redémarrage du service sans état orphelin.
