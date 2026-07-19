# Pipeline client / serveur PropHunt

Ce dossier est le point d'entree unique pour construire et publier PropHunt.
Les fichiers `.cmd` peuvent etre lances directement depuis l'Explorateur ; ils evitent le blocage local des scripts PowerShell.

Le candidat courant est `P9.1 / 0.1.1907001`. Chaque build et push exécute d'abord
`BuildTools/Version/Test-PropHuntVersion.ps1`; toute divergence client/serveur/gateway/NOVA bloque le pipeline.

Sorties fixes :

- client Steam : `package/client/Windows` ;
- serveur Linux : `package/server/LinuxServer` ;
- bundle VPS : `package/vps-bundle`.

```powershell
# Build incremente client + serveur (choix normal pour une iteration)
.\DeployClientServer\Build-ClientServer.ps1

# Build complet avant une beta importante
.\DeployClientServer\Build-ClientServer.ps1 -FullCook

# Publier les packages deja qualifies
.\DeployClientServer\Push-Client-Steam.ps1
.\DeployClientServer\Push-Server-Vps.ps1

# Tout enchainer, ou republier les packages sans rebuild
.\DeployClientServer\Deploy-All.ps1
.\DeployClientServer\Deploy-All.ps1 -SkipBuild
```

Le build utilise exclusivement `PropHunt.uproject` et `K:\UE58`. Le mode normal ajoute `-iterate` pour reutiliser le cook et le DDC ; `-FullCook` reste la gate de release finale. Steam refuse un package sans P9, NOVA et WaitingRoom. Le push VPS refuse de remplacer la release si un worker PropHunt est actif et conserve une seule release de rollback.

SteamCMD et `steam_build.config.ini` restent locaux et ignorés par Git sous `BuildTools/Steam/`. Aucun chemin vers
un autre projet Unreal ni aucun identifiant Steam n'est versionné.

Incidents connus, chemins canoniques et diagnostic SteamCMD/VPS :
`SourceArt/Docs/22_DEPLOY_PIPELINE_TROUBLESHOOTING.md`.
