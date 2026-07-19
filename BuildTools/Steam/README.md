# Steam beta PropHunt

Configuration réservée au projet canonique :

- App ID : `1551300`
- Depot Windows beta : `1551301`
- branche cible : `beta`
- projet : `C:/Users/Thomas/Documents/Unreal Projects/PropHunt/PropHunt.uproject`
- moteur : `K:/UE58`

`app_build_1551300.vdf` pointe uniquement vers le client courant fixe `package/client/Windows`. Le package doit
etre Shipping, protocole 9, NOVA et contenir WaitingRoom. Les exclusions du depot retirent `*.pdb` et
`steam_appid.txt`. Aucun identifiant Steam, mot de passe ou token ne doit etre ajoute au depot.

Le validateur P9 a reussi le 2026-07-19 : EXE wrapper SHA-256
`a6406ad31b3f40cedb3082bc9145b66a4b11a35cd484dcfaa4a5a27982e5b4d5`, UTOC
`420ccd3a5babba2e2d074f5aecef2332bfabc473fd2c9811fd681ed74c10e40f`. Le client a ete publie uniquement sur
`beta` : BuildID `24281930`, manifest depot `1551301` `2052834125204237865`. La branche publique n'a pas ete
modifiee. L'upload normal passe par `DeployClientServer/Push-Client-Steam.cmd` et le SteamCMD authentifie existant.

Avant tout upload, valider que les deux VDF ciblent le même package, que `L_PH_WaitingRoom` est réellement
cuisinée, que les exclusions privées sont présentes et qu'aucun `steam_appid.txt` n'est embarqué :

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File '.\BuildTools\Steam\Test-PropHuntSteamPackage.ps1'
```

Commande d'upload à exécuter seulement après validation du package et des droits de redistribution :

```powershell
.\DeployClientServer\Push-Client-Steam.cmd
```

Le BuildID `24247554` (manifest depot `1181572483781462574`) a été poussé le 2026-07-16 sur `beta` pour un test interne privé, avec l'autorisation
explicite du propriétaire de conserver temporairement les personnages de référence cuisinés. Cette exception ne
vaut pas pour une diffusion publique : retirer ces assets du cook Steam et les remplacer par des contenus
redistribuables ou par la présentation graybox dès la fin de la campagne de tests.

Le correctif matchmaking/menu Échap a été poussé uniquement sur la branche privée `beta` le 2026-07-16 :
BuildID `24247896`, manifest depot `3036269945368643964`.

Le correctif de jonction protocole 2 a été poussé uniquement sur `beta` le 2026-07-17 : BuildID `24248610`,
manifest depot `7052549297473081026`. Il utilise `BuildIdOverride=2`, attend jusqu'à `30 s` une connexion Steam
encore `USOCK_Pending` et active temporairement les logs Shipping sous `%LOCALAPPDATA%/PropHunt/Saved/Logs`.
Utiliser Proton Experimental pour le test Linux actuel. Désactiver les logs Shipping et retirer les contenus de
référence non redistribuables après la campagne de test privée.

Le log de cette beta a révélé le vrai défaut transport : `IpNetDriver` recevait l'adresse `steam.<SteamID>` et
échouait en DNS avec `SE_HOST_NOT_FOUND`. Le correctif SteamSockets protocole 3 a été poussé uniquement sur
`beta` le 2026-07-17 : BuildID `24248954`, manifest depot `7673420938486712641`. Le projet active désormais
`SteamSockets`, utilise `/Script/SteamSockets.SteamSocketsNetDriver`, `BuildIdOverride=3` et affiche
`PROTOCOLE RESEAU 3`.

La beta physique jouable/lobby `60 s` utilise le protocole réseau `4` et a été poussée uniquement sur `beta` le
2026-07-17 : BuildID `24249774`, manifest depot `1955893278242918081`. Le package source contient `81` fichiers
et reste sous `package/ship/client/Windows`. La lecture Steam authentifiée confirme que la branche `beta` pointe
sur ce BuildID et ce manifest ; la branche publique n'a pas été modifiée.

Le correctif spectateur/piquet et audio du Super Poulet jouable passe en protocole `5` et remplace cette beta le
2026-07-17 : BuildID `24250476`, manifest depot `489858131775756902`. Le package final contient `80` entrées,
dont les prérequis Visual C++ ; le log de jeu accidentel de l'ancien package n'est plus inclus. La lecture Steam
authentifiée confirme `beta=24250476` et `public=24216953`.

Le candidat protocole `7` du 2026-07-18 conserve les joueurs dédiés sans rôle pendant le Lobby, choisit le
Tueur aléatoirement côté serveur à la fermeture du roster, laisse `15 s` de fenêtre joinable lorsque le roster
réservé est atteint et autorise le 1v1 immédiat uniquement si les deux joueurs marquent explicitement prêt.
La relecture externe, l'automation `25/25`, le validateur du package et le smoke Steam Linux P7 sont réussis.
Après autorisation explicite d'utiliser uniquement le SteamCMD authentifié existant, ce client final2 a été
publié sur la branche privée `beta` le 2026-07-18 : BuildID `24276270`, manifest depot
`8793083830627277218`. SteamPipe a mappé `75` fichiers / `504 MB` et envoyé `213` nouveaux chunks. Une lecture
Steam authentifiée confirme `beta=24276270` et manifest `8793083830627277218`; la branche publique est restée
sur BuildID `24216953`, manifest `6133896315375195351`. La jonction multi-comptes P7 reste une campagne de test
séparée ; cette publication privée ne vaut pas diffusion publique.
