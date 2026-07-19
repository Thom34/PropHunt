# Pipeline modulaire client / serveur PropHunt

Ce dossier est le point d'entree unique pour construire et publier PropHunt.
Les fichiers `.cmd` peuvent etre lances directement depuis l'Explorateur ; ils evitent le blocage local des scripts PowerShell.

La publication active est `P9.1 / 0.1.1907001`. Les sources locales ayant changé depuis ce BuildID, le candidat
Shipping local est désormais `0.1.1907002`. Chaque build et push exécute d'abord
`BuildTools/Version/Test-PropHuntVersion.ps1`; toute divergence client/serveur/gateway/NOVA bloque le pipeline.

## Politique Git de `BuildTools`

Le dossier `BuildTools` apparaît volontairement dans le dépôt GitHub. Les fichiers génériques nécessaires pour
reproduire une release doivent rester versionnés : manifeste et contrôle de version, manifests/validation
SteamPipe, générateur de bundle VPS, scripts Linux, unité systemd et documentation d'exploitation. Les retirer du
suivi casserait un clone neuf et rendrait le pipeline dépendant de fichiers connus d'une seule machine.

Seuls les éléments locaux ou authentifiés restent hors Git :

- `BuildTools/Steam/steamcmd/` ;
- `BuildTools/Steam/steam_build.config.ini` ;
- identifiants, mots de passe, tokens, clés, caches et journaux SteamCMD.

La présence de `BuildTools` sur GitHub n'est donc pas une fuite en elle-même. Avant un push, vérifier qu'aucun
fichier privé n'est suivi avec `git ls-files BuildTools/Steam/steamcmd BuildTools/Steam/steam_build.config.ini` :
la commande ne doit rien retourner. Ne pas ignorer ou désindexer tout `BuildTools/`.

Sorties fixes :

- client Steam : `package/client/Windows` ;
- serveur Linux : `package/server/LinuxServer` ;
- bundle VPS : `package/vps-bundle`.

```powershell
# Voir le plan sans aucune mutation
.\DeployClientServer\Deploy-All.cmd -Module All -PlanOnly

# Gate locale complète : gateway, Unreal, scripts, version et systemd
.\DeployClientServer\Test-ReleaseReadiness.cmd

# Gate complète : build, VPS coordonné, puis Steam beta en dernier
.\DeployClientServer\Deploy-All.cmd -Module All -FullCook

# Modules indépendants
.\DeployClientServer\Deploy-All.cmd -Module Validate
.\DeployClientServer\Deploy-All.cmd -Module Build -FullCook
.\DeployClientServer\Deploy-All.cmd -Module Vps
.\DeployClientServer\Deploy-All.cmd -Module Steam
```

Le build utilise exclusivement `PropHunt.uproject` et `K:\UE58`. Le mode normal ajoute `-iterate` pour reutiliser
le cook et le DDC ; `-FullCook` reste la gate de release finale. Steam refuse un package sans P9, NOVA et
WaitingRoom. Le push VPS coordonné sauvegarde gateway/NOVA/SQLite, refuse tout worker actif, installe la release,
bascule NOVA, vérifie le health et restaure le plan de contrôle précédent en cas d'échec.
Le module Steam interroge le health VPS et refuse l'upload si sa version ou son protocole diffère du client.
Le contrat lobby `display_name/survivor_players` impose de déployer gateway + serveur/NOVA avant le nouveau client ;
un client local lancé contre l'ancienne gateway peut recevoir `HTTP 400 invalid_contract`.

SteamCMD et `steam_build.config.ini` restent locaux et ignorés par Git sous `BuildTools/Steam/`. Aucun chemin vers
un autre projet Unreal ni aucun identifiant Steam n'est versionné.

Incidents connus, chemins canoniques et diagnostic SteamCMD/VPS :
`SourceArt/Docs/22_DEPLOY_PIPELINE_TROUBLESHOOTING.md`.

Procédure opérateur complète, modules, rollback et preuves :
`DeployClientServer/DEPLOYMENT_RUNBOOK.md`.

Matrice incident → correctif → test, conditions NO-GO et test humain :
`SourceArt/Docs/24_RELEASE_QUALITY_GATE.md`.
