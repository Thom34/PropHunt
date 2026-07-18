# Exploitation Linux du serveur dédié PropHunt

Ce dossier complète le package Linux cuisiné. Il ne contient aucun secret, ne modifie pas automatiquement le
pare-feu et n'effectue aucun déploiement distant.

Gate propriétaire : ne lancer aucune connexion SSH, copie distante, modification de pare-feu ou installation
systemd sur le VPS sans prévenir Thomas et attendre ses explications puis son accord explicite.

## Arborescence VPS retenue

- package Unreal : `/opt/prophunt/server` ;
- scripts de ce dossier : `/opt/prophunt/ops` ;
- configuration locale : `/etc/prophunt/prophunt-server.env` ;
- état et HOME du compte : `/var/lib/prophunt` ;
- logs : `/var/log/prophunt` ;
- runtime SteamCMD 64 bits : `/opt/steamcmd/linux64/steamclient.so`.

Le service s'exécute sous le compte non privilégié `prophunt`. Le protocole client et serveur doit rester `6`
pour ce lot.

## Génération du bundle depuis Windows

Depuis la racine canonique du projet :

```powershell
& '.\BuildTools\Server\New-PropHuntVpsBundle.ps1' `
  -OutputDirectory '.\package\goal-final-20260717\vps-bundle-final'
```

Le générateur refuse tout chemin extérieur au dépôt et tout écrasement. Par défaut, il exclut uniquement les
symboles séparés `PropHuntServer.debug`/`.sym`, crée les archives serveur/ops, un manifeste protocole 6 et
`SHA256SUMS`. Utiliser `-IncludeDebugFiles` seulement pour produire volontairement une archive de diagnostic.

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
sudo chmod 0750 /opt/prophunt/server/PropHunt/Binaries/Linux/PropHuntServer
sudo chmod 0750 /opt/prophunt/ops/run-prophunt-server.sh /opt/prophunt/ops/preflight-prophunt-server.sh \
  /opt/prophunt/ops/test-vps-bundle-local.sh
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
  /chemin/vers/PropHuntServer-Linux-Protocol6-Runtime.tar.gz \
  /chemin/vers/PropHuntServer-Ops-Protocol6.tar.gz
```

Ce test utilise un répertoire `mktemp` borné sous `/tmp`, normalise les modes, exécute le pré-vol, lance le
serveur vingt secondes avec `-NoSteam`, vérifie le chargement de la carte et l'arrêt réseau, puis nettoie
uniquement ce répertoire temporaire.

## Pré-vol et service

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
4. découverte et jonction depuis un second compte Steam protocole 6 ;
5. premier client Hunter, suivant Prop, puis passage en Traque ;
6. déconnexion, arrêt et redémarrage du service sans état orphelin.
