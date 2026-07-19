# Runbook de publication PropHunt

Ce document est la procédure opérateur canonique pour construire, déployer et prouver une release PropHunt.
Il s'applique au projet `C:\Users\Thomas\Documents\Unreal Projects\PropHunt\PropHunt.uproject` avec le moteur
`K:\UE58`. Il ne faut jamais remplacer ces chemins par un projet récent d'Unreal ou par un autre checkout.

## Publication technique courante

| Champ | Valeur |
|---|---|
| Série | `P9.1` |
| Version | `0.1.1907001` |
| Protocole | `91` |
| Release VPS/NOVA | `build-prophunt-v0.1.1907001` |
| Steam beta BuildID | `24283752` |
| Manifest dépôt `1551301` | `3076020675353043028` |
| Exécutable client SHA-256 | `a6406ad31b3f40cedb3082bc9145b66a4b11a35cd484dcfaa4a5a27982e5b4d5` |
| IoStore UTOC SHA-256 | `4129390e50c86d099ffd3021b81e046938c36d85d3ee4a4f868e5f3d9014f9c6` |

Gateway, NOVA, serveur Linux et client Steam beta sont publiés. Le test humain final à deux comptes reste une
gate distincte et ne doit pas être déduit du succès de SteamPipe.

Cette table décrit la publication active, pas le checkout actuel. Les sources locales ont changé après ce
BuildID ; avant tout nouveau build/push Shipping, incrémenter vers `0.1.1907002` ou supérieur. Le nouveau contrat
lobby `display_name/survivor_players` exige un déploiement gateway + serveur/NOVA avant Steam ; l'ancienne gateway
refuse le nouveau champ strict avec `HTTP 400 invalid_contract`.

## Résumé en une commande

Après la préparation locale Steam décrite plus bas :

```powershell
.\DeployClientServer\Deploy-All.cmd -Module All -FullCook
```

L'ordre est imposé :

1. gate complète version/scripts/gateway/Unreal ;
2. build/cook client et serveur puis validation des deux packages ;
3. déploiement VPS coordonné gateway + unité systemd + release Linux + route NOVA ;
4. contrôle `/healthz`, permissions, tickets et outboxes ;
5. nouvelle lecture du health exact puis publication du client Steam `beta` en dernier.

Un échec avant Steam arrête le pipeline. Le client n'est donc jamais publié vers un plan de contrôle qui n'est
pas prêt à accepter exactement sa version.

Le module VPS coordonné exige le checkout privé `SourceArt` présent localement, car il embarque le paquet gateway
et ses tests. Il refuse explicitement d'opérer si ce paquet n'est pas disponible.

Pour afficher le plan sans build, SSH, SCP, restart ou upload Steam :

```powershell
.\DeployClientServer\Deploy-All.cmd -Module All -PlanOnly
```

## Source de vérité

`BuildTools/Version/PropHuntVersion.json` porte la version complète. Le contrôle
`BuildTools/Version/Test-PropHuntVersion.ps1` refuse le pipeline si ses projections divergent :

- série réseau, par exemple `P9.1` ;
- release client/serveur, par exemple `0.1.1907001` ;
- protocole entier Unreal/gateway, par exemple `91` ;
- `BuildIdOverride` Steam ;
- `build_id` et nom de release NOVA ;
- description SteamPipe.

Une itération déjà publiée n'est jamais reconstruite sous le même identifiant. Incrémenter d'abord la version,
exécuter `-Module Validate`, puis seulement construire.

## Modules opérateur

| Besoin | Commande | Mutation externe |
|---|---|---|
| Vérifier la cohérence | `Deploy-All.cmd -Module Validate` | aucune |
| Construire les deux packages | `Deploy-All.cmd -Module Build -FullCook` | aucune |
| Déployer tout le VPS | `Deploy-All.cmd -Module Vps` | gateway, release et route NOVA |
| Déployer seulement le binaire serveur | `Deploy-All.cmd -Module ServerOnly` | release Linux seulement |
| Publier seulement le client | `Deploy-All.cmd -Module Steam` | branche Steam `beta` |
| Tout publier | `Deploy-All.cmd -Module All -FullCook` | VPS puis Steam |

Les scripts unitaires restent utilisables directement :

```powershell
.\DeployClientServer\Build-ClientServer.cmd -Target Client
.\DeployClientServer\Build-ClientServer.cmd -Target Server -FullCook
.\DeployClientServer\Push-Coordinated-Vps.cmd
.\DeployClientServer\Push-Server-Vps.cmd -BundleMode Auto
.\DeployClientServer\Push-Client-Steam.cmd
```

`ServerOnly` est volontairement un outil de maintenance. Pour une nouvelle version visible par les joueurs,
utiliser `Vps` afin de garder gateway, NOVA et serveur alignés.

## Préparation locale unique de SteamCMD

Les deux emplacements privés attendus sont :

```text
BuildTools/Steam/steamcmd/steamcmd.exe
BuildTools/Steam/steam_build.config.ini
```

Ils sont ignorés par Git. Le fichier INI contient le compte de build et ne doit jamais être affiché, journalisé,
commité ou transmis au VPS. Il peut omettre le mot de passe si SteamCMD dispose déjà d'une session authentifiée.

Le pipeline PropHunt ne lit aucun autre projet. Si l'opérateur veut importer une installation SteamCMD existante,
il le fait une seule fois avec une autorisation explicite, puis vérifie que les deux fichiers ci-dessus existent.
Le VDF utilisé reste toujours `BuildTools/Steam/app_build_1551300.vdf` : AppID `1551300`, dépôt `1551301`,
branche privée `beta`.

## Packages canoniques

| Artefact | Chemin fixe |
|---|---|
| Client Win64 Shipping | `package/client/Windows` |
| Serveur Linux Shipping | `package/server/LinuxServer` |
| Bundle de transfert VPS | `package/vps-bundle` |
| Logs SteamPipe | `Saved/SteamBuildOutput` |
| Anciennes générations de bundle | `Saved/Deployments/bundle-backups` |
| Provenance du build | `package/client/build-provenance.json`, `package/server/build-provenance.json` |

Ne jamais choisir manuellement un ancien dossier `phase*`, `final*` ou daté comme source de publication.
Un package sans provenance, ou dont l'empreinte ne correspond plus aux sources/config/assets/gateway courants,
est refusé même si son numéro de version est correct.

Le paramètre `-BundleMode` contrôle le bundle VPS :

- `Auto` : réutilise le bundle complet de la bonne version, sinon archive l'ancien et le régénère ;
- `Reuse` : refuse si les quatre fichiers attendus ne sont pas déjà présents ;
- `Regenerate` : archive toujours le bundle existant puis en crée un nouveau.

Les SHA-256 sont recalculés localement avant SCP puis revérifiés sur Debian.

## Déroulé du module VPS coordonné

`Push-Coordinated-Vps.ps1` réalise une transaction opérationnelle bornée :

1. exécute la gate release et les tests gateway/Unreal ;
2. refuse si un worker, ticket actif, allocation ou annulation PropHunt est présent ;
3. vérifie que le contrat NOVA versionné est déjà installé ;
4. arrête seulement `prophunt-gateway` ;
5. sauvegarde `/opt/prophunt-gateway`, l'unité systemd, les configurations gateway/NOVA et la base SQLite ;
6. installe paquet Python + unité systemd, configure protocole/build et attend le socket NOVA jusqu'à `30 s` ;
7. laisse la gateway arrêtée pendant l'installation du serveur ;
8. transfère le bundle, valide ses hashes, ses modes Linux, son ELF, Steamworks et ses dépendances ;
9. active `/home/ue-game/releases/<nova_release_name>` avec une release de rollback ;
10. remplace l'unique route `build-prophunt*` dans `/etc/nova-orchestrator/config.toml` ;
11. valide le TOML, redémarre uniquement `nova-orchestrator` et attend son socket ;
12. redémarre la gateway, exige un `/healthz` exact puis recontrôle permissions, tickets et outboxes ;
13. conserve la sauvegarde sous `/home/ue-game/backups/prophunt-<deployment_id>`.

Le module ne redémarre pas nginx, MariaDB, `nova-worker-broker` ou les services d'un autre jeu. Il ne modifie pas
`/home/ue-game/current`.

Si la préparation, le push serveur, la route ou le health échouent, le script restaure automatiquement gateway,
config NOVA, config gateway et SQLite depuis la sauvegarde. La nouvelle release Linux peut rester installée mais
elle n'est plus routée ; c'est sûr et utile pour le diagnostic.

## Pré-vol avant une publication importante

```powershell
.\DeployClientServer\Deploy-All.cmd -Module Validate
.\DeployClientServer\Deploy-All.cmd -Module Build -FullCook
.\DeployClientServer\Deploy-All.cmd -Module All -PlanOnly
```

`Validate` exécute désormais à lui seul compilation Python, `37/37` tests gateway, toute l'automation
`PropHunt.*`, cohérence de version, syntaxe PowerShell, contrat systemd et `git diff --check`. Ne jamais utiliser
`-SkipGatewayTests` ou `-SkipUnrealTests` sur le candidat final.

Pour une modification du contrat NOVA lui-même, le changement doit d'abord passer sa suite ciblée et la suite
MariaDB NOVA. Le module VPS coordonné vérifie la présence du contrat versionné mais n'applique pas silencieusement
un nouveau patch d'infrastructure.

## Preuves à conserver après publication

### VPS

```bash
systemctl is-active nova-orchestrator prophunt-gateway nova-worker-broker nginx
curl -fsS http://127.0.0.1:8790/healthz
grep -E 'build-prophunt' /etc/nova-orchestrator/config.toml
systemctl list-units 'nova-unreal-worker@*.service' --state=running --no-pager
```

Le health doit retourner `ok=true`, le protocole du manifeste et la version complète. Hors smoke, aucun worker ne
doit rester actif.

### Steam

Dans `Saved/SteamBuildOutput/app_build_1551300.log`, conserver la ligne :

```text
Successfully finished AppID 1551300 build (BuildID ...)
```

Conserver aussi le nouveau manifest du dépôt `1551301`. Un nouveau candidat doit produire un nouveau BuildID et
un nouveau manifest ; ne jamais recopier les identifiants de la release précédente.

### Test humain final

Avec deux comptes Steam réellement mis à jour : invitation ou inscription, même salon, countdown, allocation,
WaitingRoom, gameplay, Results, retour menu, nouvelle inscription, puis arrêt du worker et libération des ports.
Ce test reste une gate humaine : l'automatisation ne doit pas le déclarer réussi à la place des joueurs.
Le scénario détaillé incluant sortie de file, soin, objectif, porte, Results et réinscription est dans
`SourceArt/Docs/24_RELEASE_QUALITY_GATE.md`.

## Diagnostic rapide

- `client_update_required` : comparer la version client, `/healthz` et la route NOVA.
- `HTTP 400 invalid_contract` dès l'inscription : vérifier si un nouveau client `display_name` pointe encore vers
  la gateway `0.1.1907001`; déployer coordonné, ne pas retirer le champ pour contourner le schéma.
- bundle refusé car existant : utiliser `-BundleMode Auto` ou `Regenerate`, jamais supprimer au hasard.
- permission sous `/home/ue-game/releases` : le script d'activation doit passer par `sudo -n`.
- bits exécutables absents : le push normalise les modes après extraction Windows.
- gateway inactive après NOVA : vérifier le socket `/run/nova-orchestrator/api.sock`, puis les journaux des deux
  services ; ne pas redémarrer toute la machine.
- SteamCMD absent : préparer les deux fichiers privés locaux, sans modifier le VDF ni emprunter le script d'un
  autre projet.

Les incidents historiques détaillés restent consignés dans
`SourceArt/Docs/22_DEPLOY_PIPELINE_TROUBLESHOOTING.md`.
