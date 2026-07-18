# PropHunt

Jeu multijoueur asymétrique développé avec Unreal Engine 5.8.

- 1 Hunter en première personne.
- 1 à 4 Props en troisième personne.
- Combat de mêlée et capacités, sans arme à distance.
- Objectifs coopératifs, transformation en objets, capture et évasion.
- Distribution PC via Steam uniquement.

## Contenu du dépôt

Ce dépôt contient le projet reproductible : configuration Unreal partagée, code C++ et assets Unreal validés sous `Content/PropHunt`.

La documentation interne et les fichiers de création externes sont versionnés séparément et exclus de ce dépôt public. Les expériences temporaires vivent sous `Content/Developers/<Utilisateur>/` et ne doivent jamais être référencées par le contenu de production.

## Architecture

Le gameplay, l'autorité réseau et les validations sont écrits en C++. Les valeurs, animations, sons et variantes visuelles sont exposés aux Blueprints et aux Data Assets. Le jeu peut instancier des acteurs et assets cuisinés pendant une partie, mais ne crée pas de nouveau contenu procédural au runtime.

## Ouvrir et compiler correctement le projet

- Projet canonique : `C:\Users\Thomas\Documents\Unreal Projects\PropHunt\PropHunt.uproject`.
- Moteur canonique : checkout source UE 5.8 sous `K:\UE58`.
- Ne jamais lancer l'éditeur sans lui passer directement ce `.uproject`.

```powershell
# Ouvrir l'éditeur sur le bon projet
& 'K:\UE58\Engine\Binaries\Win64\UnrealEditor.exe' 'C:\Users\Thomas\Documents\Unreal Projects\PropHunt\PropHunt.uproject'

# Build Editor
& 'K:\UE58\Engine\Build\BatchFiles\Build.bat' PropHuntEditor Win64 Development 'C:\Users\Thomas\Documents\Unreal Projects\PropHunt\PropHunt.uproject' -WaitMutex -NoHotReload

# Build jeu
& 'K:\UE58\Engine\Build\BatchFiles\Build.bat' PropHunt Win64 Development 'C:\Users\Thomas\Documents\Unreal Projects\PropHunt\PropHunt.uproject' -WaitMutex -NoHotReload
```

Les règles opératoires complètes sont dans `AGENTS.md`. Le statut interne courant et le handoff Phase 1 sont
respectivement dans `SourceArt/Docs/PROJECT_STATUS.md` et `SourceArt/Docs/08_PHASE1_HANDOFF.md` lorsque le
checkout privé `SourceArt` est présent.

## État actuel

Le Game Framework réseau minimal, les objectifs, le cycle Au sol/transport/rétention/libération, six Blueprints
de liaison, le Hunter FPS graybox et le survivant FPS/TPS sont implémentés. Une attaque de mêlée graybox
autoritaire et verticalement tolérante est disponible sur clic gauche. Le Prop peut aussi copier
avec `E` deux formes de test authored et cuisinées (caisse A et tonneau B), passer de A à B, puis revenir à sa
forme initiale avec `R`. Le serveur valide rôle, phase, cooldown, allowlist, distance, ligne de vue depuis le
personnage et emplacement de la hitbox simple authored avant de répliquer la position corrigée, la forme, ses
dimensions et le résultat. La capsule racine reste nécessaire à `ACharacter`, mais les capsules de déplacement
Hunter/Prop s'ignorent entre elles ; les hitboxes query-only (boîte caisse, capsule tonneau) servent à la
sélection, à la mêlée et au placement. Les meshes restent visuels. Un dégagement serveur borné à `25 cm` est
tenté uniquement lorsque la cible copiée est le seul bloqueur ; murs, plafonds et espaces réellement trop petits
restent refusés. Les
événements Blueprint restent uniquement cosmétiques.

Les déplacements clavier acceptent simultanément `ZQSD` (AZERTY) et `WASD` (QWERTY), avec `Espace` pour
sauter. `E` transforme le Prop, `R` restaure sa forme initiale et le clic gauche déclenche la mêlée Hunter.

La présentation graybox initiale du Prop est capturée par le C++ avant l'application possible d'un état
répliqué. Le client propriétaire voit donc son cube de base dès son spawn, puis le mesh authored sélectionné
après une transformation acceptée et de nouveau le cube après un retour initial.

Les valeurs restent provisoires : les objectifs graybox sont jugés beaucoup trop rapides et le modèle cible
doit demander plusieurs coups au Hunter au lieu d'une mise au sol immédiate. Le jeu utilise actuellement
5 objectifs actifs / 4 requis ; la cible suivante est 6 / 4, puis une porte à ouvrir avant l'évasion.
Aucun contenu n'est généré procéduralement au runtime.

Dans le checkout de développement interne, un premier asset étalon de physique est également disponible sans
remplacer la transformation graybox : le `ScreamingChicken` utilise son Static Mesh authored à quatre LOD, deux hulls convexes UCX, une masse de
`8,3912 kg` et un son d'impact. `APHPhysicsPropPrototype` simule le rigid body sous Chaos, réplique son mouvement
à `30 Hz` et laisse exclusivement le serveur valider puis multicaster, à la position du contact, les impacts
dépassant `50 x masse`. C'est désormais un Pawn de test jouable : `ZQSD/WASD` applique un couple et une traction
hors centre relatifs au yaw caméra, la souris pilote la caméra TPS, `Espace` saute et maintenir `Maj gauche`
teste le redressement autoritaire. Le pivot caméra reste vertical en espace monde lorsque le mesh bascule. Aucun
petit saut automatique n'est actif (`MovementHopImpulse = 0`) : le saut est une action distincte validée au sol
par le serveur. La carte
éclairée `/Game/PropHunt/Tests/L_PH_PhysicsPropTest` permet de juger ce feeling sans cycle de chute automatique.
Ce prototype reste séparé des formes caisse/tonneau et n'ajoute aucun dégât, destruction ou capture.
Ses fichiers de référence ne sont pas redistribués dans le dépôt public tant que leur licence de publication
n'est pas validée.

La gate locale a été validée avec un listen server et une fenêtre client PIE séparée sur loopback : possession,
déplacements, sauts, vues FPS/TPS, transformation A/B, retour initial, refus trop loin/derrière un mur/emplacement
bloqué, dégagement borné près de la cible, mêlée au même niveau et légèrement surélevée, rat sans visée adaptée,
réplication et nettoyage après déconnexion. La gate sur une seconde machine physique reste à effectuer
avant de considérer ce bloc multijoueur de Phase 1 comme terminé.

Le matchmaking Steam natif est maintenant intégré pour l'App `1551300` : choix Tueur, Survivant ou Aléatoire,
priorité au plus ancien lobby disponible et quota souple de deux lobbies Tueur visibles. Le checkout fournit
également une cible serveur dédié Win64/Linux, des packages client/serveur alignés sur le protocole `6` et un kit
d'exploitation sous `BuildTools/Server/Linux`. Les prochaines gates externes sont la publication/découverte sur
Linux réel, une jonction depuis un second compte Steam et le parcours complet Results puis Lobby.

`Plugins/ThomasEditor` est un outil Editor-only local au projet. Codex le découvre uniquement dans ce dépôt via
`.codex/config.toml` ; il ne contient aucune règle de gameplay et n'est pas inclus dans le build jeu.
