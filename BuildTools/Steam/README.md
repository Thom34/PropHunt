# Steam beta PropHunt

Configuration réservée au projet canonique :

- App ID : `1551300`
- Depot Windows beta : `1551301`
- branche cible : `beta`
- projet : `C:/Users/Thomas/Documents/Unreal Projects/PropHunt/PropHunt.uproject`
- moteur : `K:/UE58`

`app_build_1551300.vdf` attend actuellement le package Windows Shipping sous
`package/ship/client/Windows`. Aucun identifiant Steam, mot de passe ou token ne doit être ajouté au dépôt.

Commande d'upload à exécuter seulement après validation du package et des droits de redistribution :

```powershell
steamcmd +login <compte_build_steam> +run_app_build "C:\Users\Thomas\Documents\Unreal Projects\PropHunt\BuildTools\Steam\app_build_1551300.vdf" +quit
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
