# ThomasEditor 0.4

ThomasEditor est le plugin **Editor-only**, natif UE 5.8 et local à PropHunt qui permet à Codex et aux outils
Unreal d'observer puis d'éditer le projet par des opérations C++ typées et gardées.

Il n'ajoute aucun module runtime, aucune règle de gameplay ou de réplication, aucun serveur propre, aucun script
Python ThomasEditor et aucun asset de production. Le transport est le plugin Epic `ModelContextProtocol` sur
`http://127.0.0.1:8000/mcp`. Avec `bEnableToolSearch=true`, le client ne reçoit que trois méta-tools :
`list_toolsets`, `describe_toolset` et `call_tool`. Les toolsets et leurs schémas UHT sont découverts à la demande.

## Architecture légère

- `ThomasEditorCore` : types, policy, façades de toolsets, ressources MCP et `Tools > Thomas Workbench`.
- `ThomasEditorAssets` : Asset Registry, dépendances/référents et Details, y compris feuilles de structs imbriqués.
- `ThomasEditorBlueprint` : Blueprint, Widget Blueprint, Interface, Actor Component et Editor Utility.
- `ThomasEditorWorld` : lifecycle Maps, niveau courant, acteurs/composants, Decal, lighting/Lumen, Post Process,
  World Partition, Data Layers, Foliage et Landscape.
- `ThomasEditorRender` : Static Mesh et Nanite.
- `ThomasEditorMaterials` : graphe Material, compilation et diagnostics structurels.
- `ThomasEditorGameplayData` : Enhanced Input et inventaire gameplay/data.
- `ThomasEditorDataAI` : Data Asset/Table et graphes Blackboard/Behavior Tree/EQS/StateTree.
- `ThomasEditorCinematics` : Level Sequence, bindings possessable/spawnable, caméras, Camera Cuts, Director
  Blueprint events, pistes, sections, channels et keyframes.
- `ThomasEditorPaper2D` : Sprite, Flipbook, Tile Set, Tile Map et Atlas.
- `ThomasEditorAnimation` : inspection Animation/Character, création liée à un Skeleton ou Skeletal Mesh
  explicite, structure des Animation Blueprints/state machines et authoring natif IK Rig/IK Retargeter/Control Rig,
  dont le premier sous-ensemble de graphe RigVM typé.
- `ThomasEditorFX` : authoring Niagara System/Emitter, paramètres, stacks/modules et renderers.
- `ThomasEditorAudio` : assets Audio, Details imbriqués et graphes MetaSound.
- `ThomasEditorPCG` : PCG Graph/Graph Instance, paramètres/overrides, classes settings, nodes, pins/arêtes et
  exécution standalone Asset suivie par job.
- `ThomasEditorValidation` : Data Validation et Map Check.
- `ThomasEditor` : compatibilité native des sept contrats historiques v0.2, chargée seulement si appelée.

Tous les providers ont `LoadingPhase=None`. Au repos, seul le Core est chargé : pas de scan Content global, pas
de tick et pas de mode `AllToolsets`. Un appel charge uniquement le provider demandé et ses dépendances Epic.

## Contrat de mutation

Toute mutation suit `Inspect -> Plan -> Apply -> Validate -> Save` :

- périmètre projet `/Game/PropHunt`, jamais `/Game/Developers` ;
- réponses et batches bornés ;
- plan à usage unique expirant après cinq minutes ;
- révision exacte et, lorsqu'applicable, valeur courante attendue ;
- refus des packages humains déjà sales et des fichiers en lecture seule ;
- transaction Editor, post-validation, sauvegarde explicite et rollback ;
- `SaveAssets` permet de persister ensuite un lot dirty par chemin/révision exacts sans recréer ni consommer un
  plan ; un verrou de fichier externe retourne une erreur réessayable et conserve les changements en mémoire ;
- confirmation R2 pour suppression, remplacement structurel, plugin et renderer ;
- aucun Python, aucune console et aucune réflexion universelle arbitraire.

## Capacités validées

- Assets/Details : inventaire, propriétés filtrées, structs imbriqués, object/soft paths, dépendances et référents.
- Blueprint/UMG : création normal/widget/interface/editor utility, variables scalar/object/class/interface/soft/
  struct/enum et containers array/set/map, composants, interfaces, CDO, fonctions, nœuds, pins, connexions,
  Widget Tree, animations `RenderOpacity` avec range/rate/bindings/clés et bindings de propriété UMG, avec
  compilation/sauvegarde et suppressions R2.
- World : acteurs, transforms, labels, propriétés et composants ; preuve Decal complète avec matériau assigné.
- Environment : inspection World Partition/Data Layers/Landscape/Foliage/HLOD/navigation ; création,
  configuration, hiérarchie, assignation et suppression de Data Layers privées ; ajout/déplacement/suppression
  d'instances Foliage et suppression de types avec garde-fous World Partition ; save/create/open Maps, y compris
  création native d'une carte World Partition par conversion UE, avec garde
  dirty et R2 ; Landscape plat par vrai `Import`, topology/collision/étendue, matériaux, Nanite/navigation et edit
  layers, plus inspection height/weight bornée, création native/sauvegarde de `LandscapeLayerInfoObject`, régions
  height/weight exactes, imports `.r16`/`.r8` contrôlés et brosses sculpt/paint radiales. Chaque mutation de données
  exige le hash courant, une couche d'édition exacte pour le paint, une cible définie par le Material, une confirmation
  R2, puis une relecture/hash intégrale. HLOD, NavData/Recast et NavMeshBounds disposent de détails typés. Les builders World Partition navigation,
  build HLOD et delete HLOD sont exposés comme jobs natifs plan/start/status/cancel, avec map propre, confirmation
  R2, process caché, log dédié, annulation et rechargement gardé. Le smoke dédié exécute réellement navigation,
  build HLOD et delete HLOD sur map jetable, obtient trois codes `0`, puis supprime map, HLOD Layers et external
  actors sans résidu. Les enfants commandlet désactivent leur seul auto-start MCP pour préserver le serveur principal.
  Le spawn générique d'un Landscape sans
  GUID/topologie est refusé.
- Post Process : toutes les propriétés directes réfléchies de `FPostProcessSettings`, overrides et blendables.
- Lumen : réglages renderer pertinents découverts par métadonnées et réglages Lumen du Post Process.
- Materials : création/édition/connexion/suppression de nœuds, compilation et diagnostics d'atteignabilité.
- Render : inspection Static Mesh et préflight/apply Nanite avec rebuild, save et rollback.
- Enhanced Input : Input Action, Mapping Context, type de valeur et mappings.
- Data/AI : Data Asset/Table et rows, Blackboard, graphes Behavior Tree/EQS et structure complète StateTree
  (états, tâches, conditions, évaluateurs, transitions, propriétés et suppressions gardées).
- Cinematics : création Level Sequence, rates/ranges, possessables et templates Actor spawnables ; caméras Cine
  avec transform/lentille/focus, Camera Cuts, events nommés compilés dans le Director Blueprint, pistes
  root/binding/property, sections, états et clés typées. Écrasements et suppressions structurelles restent R2.
- Paper2D : création Sprite/Flipbook/Tile Set/Tile Map, source Sprite et frames/FPS Flipbook.
- Animation : lecture/création Sequence/Montage/Blend Space/Animation Blueprint et authoring root motion,
  notifies/states, courbes, sections/liens/slots/segments Montage, sockets Skeleton, state machines, états, entrée
  et transitions, plus `Sequence Player` direct dans les state graphs, constantes et getters de variable bool
  directs dans les transition rules. Une règle peut aussi combiner le Transition Getter `time_remaining` ou
  `time_remaining_fraction`, une comparaison bornée `<`/`<=`/`>`/`>=` et un garde bool via `AND`/`OR`.
  `set_transition_rule_expression` généralise ce motif avec une expression RPN typée de 1 à 64 tokens : getters
  bool/float, constantes numériques, les neuf `ETransitionGetter` natifs, opérations numériques
  `add/subtract/multiply/divide/min/max/abs`, six comparaisons, `AND`, `OR` et `NOT`. Le
  getter `arbitrary_state_blend_weight` exige en plus un `ContextName` exact, sensible à la casse et présent dans
  la même state machine. Le préflight impose les types de pile, l'inspection reconstruit une forme canonique exacte
  depuis les pointeurs UE et le retrait est R2.
  Un state peut aussi porter un `Blend List by Bool` gardé entre deux
  Sequences, piloté par une variable bool, ou un `Blend Space Player` 1D/2D piloté par une/deux variables float.
  Un `Blend List by Int` accepte aussi 2 à 8 Sequences avec rates/temps individuels et variable d'index entière.
  Les Sequences exposent leurs réglages additifs (local/mesh space et base pose), et un state peut appliquer une
  Sequence additive sur une base via `Apply Additive` et une variable alpha float. Un
  `Layered Blend per Bone` accepte aussi une base et 1 à 8 couches Sequence ; chaque couche porte son getter
  alpha float, son os racine et sa profondeur de branch filter, avec options mesh-space rotation/scale et root
  motion sur l'os racine.
  R35 ajoute une couche générique bornée pour les nœuds `UAnimGraphNode_Base` natifs concrets déjà chargés :
  inventaire des classes admissibles, inspection exacte des nœuds/pins/liens par GUID persistant et état de graphe
  `NodeCount/PinCount/LinkCount/ContentHash`. Cinq actions ajoutent/retirent un nœud, règlent le default d'un pin
  d'entrée non lié et ajoutent/retirent un lien compatible sans remplacer implicitement une connexion. Une seule
  mutation est admise par plan, l'état complet du graphe est requis, les nœuds résultat/root/state machine restent
  protégés et remove node/link exige R2.
  R36 expose en plus les propriétés directes éditables du struct runtime dérivé de `FAnimNode_Base` embarqué dans
  chaque nœud. L'inspection publie chemin, type et valeur exacts ainsi que `PropertyCount/PropertyHash`; l'action
  `set_state_graph_node_property` accepte seulement booléens, nombres, enums, noms, chaînes et structs simples
  allowlistés. Les champs déjà matérialisés en pins sont exclus, et chaque mutation exige le GUID du nœud et le
  `ContentHash` complet du graphe avant compile/save/reload et relecture exacte.
  R37 étend les expressions de transition RPN aux huit getters natifs non arbitraires : temps courant, longueur,
  fractions, temps restant, temps écoulé/poids du state courant et durée de transition. L'allowlist Kismet pure
  ajoute `add/subtract/multiply/divide/min/max/abs` et les comparaisons `equal/not_equal`, sans accepter de classe
  ou fonction fournie par l'appelant. L'inspection reconstruit toujours l'expression canonique exacte après reload.
  R38 complète le neuvième getter natif `ArbitraryState_GetBlendWeight` avec un contexte d'état typé : le token
  porte le nom exact, la création renseigne `AssociatedStateNode`, et l'inspection exige que ce pointeur appartienne
  à la même state machine. Le contexte canonique suit un rename d'état et reste exact après save/reload.
  R39 étend les propriétés AnimGraph aux feuilles de six structs imbriqués explicitement allowlistés
  (`InputScaleBias`, `InputScaleBiasClamp`, `InputRange`, `BoneReference`, `BranchFilter`, `InputBlendPose`), aux
  éléments déjà présents des `TArray` jusqu'à 16 entrées sans redimensionnement implicite, et aux références fortes
  `AnimationAsset`/`Skeleton`/`SkeletalMesh` résolues exactement sous `/Game/PropHunt`. La récursion est bornée,
  chaque valeur passe par une copie de preflight, un import canonique strict et une relecture après mutation.
  R40 publie aussi chaque `TArray` avec chemin, type d'élément, taille, plafond et politique de resize, puis ajoute
  `resize_state_graph_node_array`. Les tableaux de configuration imbriqués dans les structs allowlistés passent de
  0 à 16 éléments sous confirmation R2 ; le preflight redimensionne une copie complète, valide tous les nouveaux
  éléments et l'apply exige le même résultat après notification, compilation, sauvegarde et reload. Les tableaux
  structurels de premier niveau couplés aux pins, tel `Node.LayerSetup`, restent explicitement non redimensionnables.
  R41 ouvre enfin les conteneurs associatifs scalaires déjà présents dans les nœuds natifs : `TMap` et `TSet`
  sont inspectés, triés canoniquement et intégrés au hash de propriété. `set_state_graph_node_property` remplace
  leur contenu complet sous confirmation R2, avec un plafond de 16 entrées, validation sur copie et readback exact.
  La preuve réelle couvre `ModifyCurve.Node.CurveMap` et `RemoveCurve.Node.Curves`, chaque mutation étant compilée,
  sauvegardée, rechargée, restaurée au hash exact puis nettoyée via Unreal.
  R42 ferme les deux références de classe réellement inventoriées : `LinkedAnimGraph` et `LinkedAnimLayer` peuvent
  recevoir leur `Node.InstanceClass` sous forme de classe générée exacte d'un `UAnimBlueprint` situé sous
  `/Game/PropHunt`. Package path seul, asset non Anim Blueprint, classe script/externe et hash périmé sont refusés ;
  l'opération est R2 car elle peut reconstruire les pins. Le smoke assigne/recharge/restaure un vrai target Anim BP.
  R43 ajoute les références fortes `UCurveFloat` utilisées par plusieurs blends et contrôleurs. Elles réemploient
  le contrat objet R39 : asset exact sous `/Game/PropHunt`, classe compatible, validation sur copie et readback.
  L'ajout reste R1 ; le smoke persiste un vrai `CurveFloat` sur `BlendListByBool.Node.CustomBlendCurve`, restaure
  `None`, puis nettoie nœud et asset avec compilation/sauvegarde/reload à chaque état.
  R44 étend ce contrat aux références fortes `UPhysicsAsset` et `UMirrorDataTable`, ce qui ferme les gaps
  `RigidBody`/`RigidBodyWithControl.Node.OverridePhysicsAsset` et `Mirror.Node.MirrorDataTable`. L'affectation reste
  R1 ; le smoke crée les deux assets exacts, refuse path externe/classe incompatible/no-op/hash périmé, affecte,
  compile/sauvegarde/recharge, restaure `None`, puis retire les nœuds sous R2 et nettoie quatorze assets temporaires.
  R45 ferme les neuf dernières références fortes `Kind=object` : les six propriétés `UBlendProfile`,
  `IKRig.Node.RigDefinitionAsset`, `RetargetPoseFromMesh.Node.IKRetargeterAsset` et
  `RigidBodyWithControl.Node.PhysicsControlAsset`. Un Blend Profile doit être le sous-objet exact de son Skeleton
  propriétaire sous `/Game/PropHunt`; IK Rig et IK Retargeter sont R2 car UE reconstruit leurs pins, tandis que
  Blend Profile et Physics Control Asset restent R1. Quatre cycles complets sauvegardés/rechargés reviennent au
  hash exact et nettoient seize assets temporaires, Skeleton dupliqué inclus, sans toucher au Skeleton de production.
  R46 ferme ensuite 41 gaps de structs de réglages réellement mesurés : `FInputAlphaBoolBlend` (29),
  `FInputScaleBiasClampConstants` (1), `FBoneSocketTarget` (5), `FAxis` (4) et `FReferenceBoneFrame` (2), avec
  descente sûre dans `FSocketReference`. Seules les feuilles `Edit` non transitoires sont exposées ; caches d'os/
  socket, état de blend et pose links restent fermés. L'événement Editor transmet maintenant la feuille modifiée
  tout en conservant son membre `UObject` racine, ce qui laisse UE reconstruire correctement les pins/libellés dépendants.
  Sequence Player, LookAt et TwistCorrective couvrent invalid/no-op/hash périmé, R1, save/reload, restauration et
  hash exact : quatorze applies passent sans erreur ni warning de compilation et sans asset temporaire résiduel.
  R47 sépare ensuite les connexions de pose des vrais réglages : 34 `FPoseLink` et 23
  `FComponentSpacePoseLink` sont maintenant comptés comme topologie pilotée par les pins/liens, jamais comme
  propriétés mutables. Six structs supplémentaires (`FRuntimeFloatCurve`, `FRotationRetargetingInfo`,
  `FAnimPhysSimSpaceSettings`, `FSimSpaceSettings`, `FRBFParams`, `FAlphaBlend`) exposent leurs feuilles `Edit`
  sûres et ferment sept occurrences top-level. L'inventaire mesuré passe de 119 à 55 vrais gaps : 43 tableaux
  structurels et 12 occurrences de structs complexes. AnimDynamics, RigidBody, SplineIK, Trail et PoseDriver
  couvrent six cycles invalid/no-op/hash périmé, R1, save/reload et restauration exacte ; les 22 applies R47 ont
  `compile_errors=0 compile_warnings=0` et ne laissent aucun asset temporaire.
  R48 ferme dix tableaux top-level réellement autonomes : les trois filtres de Dead Blending, les deux filtres
  d'Inertialization, `HandIKRetargeting.IKBonesToMove`, `PoseDriver.OnlyDriveBones`/`SourceBones` et les listes
  d'os d'entrée/sortie de Control Rig. Leur valeur complète est inspectée et remplacée atomiquement sous R2 par
  `set_state_graph_node_property`, avec export vide explicite `()`, limite 0..16 et validation de chaque
  `FBoneReference` contre le Skeleton de l'Anim Blueprint. L'inventaire publie
  `StateGraphWholeArrayPropertyCount=10` et descend de 55 à 45 vrais gaps : 33 tableaux encore spécialisés et
  12 occurrences de structs complexes. Quatre cycles Inertialization/PoseDriver couvrent invalid, limite, no-op,
  confirmation, hash périmé, save/reload et restauration exacte ; les 12 applies R48 ont
  `compile_errors=0 compile_warnings=0`, avec suite complète verte et zéro résidu.
  R49 réconcilie ensuite la réflexion avec l'authoring déjà réel : cinq `TArray<FPoseLink>` deviennent une
  métrique de topologie (`pose_link_array=5`) et sept tableaux sont attribués aux lifecycles dédiés Bool Blend
  (2), Int Blend (2) et Layered Blend per Bone (3). Trois tableaux appartiennent aux deux catégories, soit neuf
  fermetures uniques et `StateGraphResolvedStructuralArrayCount=19` avec R48. Le backlog de propriétés descend à
  36 : 24 tableaux spécialisés et 12 structs complexes. Les tableaux de pose Enum/MultiWay restent explicitement
  une dette de nombre de pins dynamique, même s'ils ne sont plus présentés comme valeurs arbitraires. Build, test
  ciblé et suite complète sont verts, avec zéro résidu.
  R50 ferme ces deux lifecycles dynamiques avec l'action R2 `resize_state_graph_pose_array`. La création d'un
  `BlendListByEnum` exige désormais un `StateGraphEnumPath` exact et une `StateGraphEnumEntries` canonique par
  pose non-default ; Enum couple `BlendPose/BlendTime` avec des temps 0..60 s, tandis que MultiWay couple
  `Poses/DesiredAlphas` avec des alphas 0..1. Taille 2..16, nombre de
  valeurs exact, hash de graphe, reconstruction des pins, purge des orphelins et readback sont vérifiés avant
  compile/save. Les métriques publient deux lifecycles/quatre propriétés couplées, portent
  `StateGraphResolvedStructuralArrayCount` à 21 et réduisent le backlog à 34 (`22 structural_array + 12 structs
  complexes`). Dix applies couvrent les cycles 2 -> 3 -> 2 des deux nœuds, sans erreur/warning ni résidu.
  R51 commence par le premier contrat de tableau lié à un asset :
  `RetargetPoseFromMesh.Node.OverrideSetsToApply`. Son remplacement complet reste R2 et borné à 16 ; chaque nom
  doit être non vide, unique et présent dans l'`IKRetargeterAsset` exact assigné au nœud. L'inventaire publie
  désormais l'`InnerType` de chacun des gaps, un lifecycle/une propriété `ik_retarget_override_sets`, porte
  `StateGraphResolvedStructuralArrayCount` à 22 et réduit le backlog à 33 (`21 structural_array + 12 structs
  complexes`). Le smoke crée un override set réel, refuse set inconnu/doublon/entrée invalide/no-op/hash périmé,
  compile/sauvegarde/recharge, restaure le tableau et nettoie nœud plus fixture sans résidu.
  Le second lot R51 ferme atomiquement `Constraint.Node.ConstraintSetup` avec son tableau parallèle
  `ConstraintWeights` via `replace_state_graph_constraint_array`. Le contrat R2 accepte 0..16 `FConstraint`,
  exige autant de poids finis dans `[0,1]`, refuse tout `TargetBone` vide ou absent du Skeleton de l'Anim
  Blueprint, prévalide sur une copie et reconstruit/purge les pins dynamiques après mutation. L'inspection publie
  valeurs canoniques, tailles, poids et nombres de pins. Les métriques ajoutent un lifecycle/deux propriétés
  `constraint_setup_weights`, portent le total résolu à 24 et le backlog à 31 (`19 structural_array + 12 structs
  complexes`). Le cycle `0 -> 1 -> 2 -> 0` compile, sauvegarde, recharge et restaure le hash exact sans résidu.
  Un `ik_rig` peut maintenant être créé contre un Skeletal Mesh exact via la factory Epic découverte à la
  demande. L'inspection retourne preview mesh, hiérarchie d'os, exclusions, goals, solvers, retarget root/root
  motion et chaînes. Les mutations typées règlent le retarget root, ajoutent/retirent goals et chaînes après
  préflight des os, de la lignée et des références ; les retraits sont R2. Les six types de solver Epic chargés
  sont découverts par leur vraie `UScriptStruct`; la pile accepte ajout/retrait, enable/disable, os de départ et
  de fin, déplacement, connexion/déconnexion de goal et ajout/retrait R2 des bone settings. Un goal relié ne peut
  pas être supprimé. Toutes ces opérations passent par les contrôleurs publics IK Rig et sont prouvées par
  plusieurs save/reload disque. Le Limb solver conserve ses dix réglages typés/bornés. En complément,
  `IKRigSetting` expose les champs directs éditables solver/goal/bone avec valeur attendue exacte, allowlist de
  types, clamps, import canonique et relecture immédiate ; objets, conteneurs et structs arbitraires restent
  fermés. Root-motion bone et exclusion d'os sont aussi configurables et inspectables.
  Un `ik_retargeter` peut également être créé contre un IK Rig exact. Le contrôleur public Epic lie les rigs
  source/cible, expose la pile et les types d'opérations réellement disponibles, puis permet ajout de la pile par
  défaut, ajout/retrait/rename/move, activation et inspection des réglages directs. Les override sets UE 5.8 sont
  inspectables et mutables avec hiérarchie, set actif et overrides de propriété directs typés, bornés et protégés
  par valeur attendue. Les poses source/cible couvrent ajout/rename/retrait, pose courante et root offset vertical ;
  X/Y sont refusés au preflight car le contrôleur ne les persiste pas. Les retraits structurels restent R2 et le
  smoke crée, sauvegarde, recharge puis nettoie IK Rig et IK Retargeter sans résidu.
  Le socle Control Rig UE 5.8 crée aussi un `ControlRigRuntimeAsset` contre un Skeletal Mesh exact via
  `UControlRigAssetFactory`, assigne le preview mesh et importe la hiérarchie par `URigHierarchyController`.
  L'inspection retourne les éléments, types, parents, transforms locales initiales et valeurs de courbe. Huit
  actions bornées ajoutent/renomment/transforment/suppriment les nulls et ajoutent/renomment/règlent/suppriment
  les courbes. Le lifecycle générique des contrôles couvre désormais `Bool`, `Float`, `Integer`, `Vector2D`,
  `Position`, `Scale`, `Rotator` et `Transform`, avec add/rename/set-value/remove, valeurs initiale/courante et
  parent Bone/Null ; les anciennes actions float restent compatibles. Une seule mutation est admise par plan,
  avec valeur attendue et R2 pour rename/remove. Les settings de forme (nom/couleur/visibilité), le transform de
  forme initial/courant et les limites min/max typées sont éditables et inspectés. R33 inspecte uniquement les
  bibliothèques de formes référencées par l'asset, expose leurs chemins/defaults/formes et refuse un nom absent avant
  mutation. Les drapeaux min/max sont pilotables par canal natif exact, avec compatibilité des anciens booléens
  globaux. Les sockets Control Rig
  couvrent add/rename/transform/settings/retrait, tandis que les espaces disponibles d'un contrôle couvrent
  ajout, label, ordre et retrait. Enfin les vingt types natifs de métadonnées couvrent bool/float/int/name/vector/
  rotator/quat/transform/color/element key et leurs tableaux, bornés à 16 valeurs, sur un type d'élément exact,
  sans toucher aux métadonnées réservées des sockets.
  L'asset runtime Control Rig est maintenant relié à son `UControlRigEditorAsset` natif : l'inspection bornée expose
  graphes, nœuds, pins récursifs et liens, variables membres/locales, fonctions locales, leurs références et leurs
  pins d'interface, ainsi que les Units/events/templates/dispatch enregistrés disponibles. Cinquante-sept actions
  RigVM couvrent les lifecycles Unit, event, variable membre ou locale, getter/setter, template/dispatch, fonction
  locale et appel, ainsi que position/taille/couleur, default de pin non lié, liens, commentaires, reroute sur lien,
  tableaux dynamiques, résolution/unresolve wildcard et lifecycle borné d'un modèle top-level. R24 ajoute
  l'interface input/output d'une fonction locale
  (add/remove/rename/type) et le rename/default/type/index d'une variable locale. R25 ajoute l'ordre des pins exposés,
  la visibilité public/private, le mode mutable/pure et les category/keywords/description d'une fonction locale.
  R26 ajoute le collapse d'un groupe exact de 2 à 32 nœuds directs et l'expansion du collapse node obtenu.
  R27 ajoute un reroute libre de type/default/widget explicites, ancré atomiquement à n'importe quel pin source ou
  cible exact, et le retypage R2 d'un template déjà résolu par un cycle unresolve/resolve transactionnel.
  R28 unifie la découverte et la mutation récursives des graphes contenus : un collapse arbitraire reste adressable
  après reload, y compris pour y créer/retirer des getter/setter de variable membre. Les variables locales sont
  réservées par l'API UE aux vraies graph functions et ce faux cas est désormais refusé au preflight.
  R29 ajoute `add_control_rig_graph` et `remove_control_rig_graph` via `IRigVMClientHost`. Le nom généré natif est
  relu (`RigVMModel <Nom>::`), le nouveau graphe accepte les mutations existantes après reload et son retrait est
  limité à un modèle top-level vide, avec état exact et confirmation R2.
  R30 croise les structs Unit avec `FRigVMRegistry` : l'inspection publie le vrai couple struct/méthode et le nom
  de fonction enregistré, puis le preflight refuse tout couple absent. Les Units ne sont plus limitées aux seuls
  modules `ControlRig`/`RigVM` ; toute `FRigVMStruct` réellement enregistrée peut être créée, sauvegardée et relue.
  R31 expose les 31 Units agrégables du registre avec leurs pins d'entrée/sortie, puis ajoute ou retire les pins
  variadiques via `URigVMController`. Le nom suivant est dérivé par UE, l'état complet du nœud est exigé, le retrait
  d'un pin lié ou de l'un des deux pins de base est refusé et toute suppression reste R2. Le premier ajout et le
  wrapper `URigVMAggregateNode` résultant sont inspectés comme un même lifecycle persistant.
  R32 complète les onze valeurs de `ERigControlType` avec `TransformNoScale`, `EulerTransform` et `ScaleFloat`.
  Les conversions utilisent les helpers natifs `URigHierarchy` et les types `FTransformNoScale`/`FEulerTransform` ;
  une échelle non unitaire sur `TransformNoScale` est refusée au preflight au lieu d'être silencieusement perdue.
  Chaque nouveau type suit le même cycle add/set/remove R2, sauvegarde et rechargement disque que les huit précédents.
  R33 ferme les trois lacunes Control Rig suivantes : validation contre les formes réellement référencées, limites
  min/max par canal natif et totalité des vingt `ERigMetadataType`, dont Quat normalisé et dix variantes tableau.
  Chaque métadonnée est posée, sauvegardée/rechargée puis supprimée sous R2 ; la surface RigVM reste à 56 actions à
  l'issue de R33.
  R34 porte la surface à 57 actions avec `rename_control_rig_graph`. Un modèle top-level même non vide peut être
  renommé ou supprimé sous R2 à partir de son état exact, qui inclut désormais un `ContentHash` déterministe de ses
  nœuds, liens et variables locales. Le save/reload conserve le commentaire du graphe renommé, puis la suppression
  non vide revient à la baseline. `ReferencedAssetPath` permet aussi à l'action d'appel existante de viser une
  fonction publique d'un second Control Rig exact sous `/Game/PropHunt`; fonction privée, asset externe dirty et
  suppression d'une fonction encore référencée sont refusés. L'inspection distingue références locales/externes.
  Le graphe, la fonction, les noms, directions/types, conflits, états attendus et références sont validés avant
  `URigVMController`; suppressions, renames, changements de type et mutable/pure restent R2, puis la VM est recompilée.
  Chaque mutation est sauvegardée puis relue après reload. La preuve fait évoluer `TE_LocalSpeed` en
  `TE_LocalRate`, default `6.250000`, type `double`, exerce l'ordre avec une seconde variable, puis vérifie la
  propagation de l'interface `InputScale -> Gain` et `ResultValue float -> double` sur un vrai appel. R25 publie
  cette fonction, persiste `ThomasEditor.Tests`, ses mots-clés/description, déplace/restaure l'ordre des deux pins et
  propage mutable -> pure -> mutable sur l'appel. R26 regroupe les deux Units et leur lien, relit le graphe contenu,
  puis les ré-étend en restaurant exactement positions, valeur de pin et lien. R27 persiste un reroute libre `float`
  avec widget/default et retype `CoreEquals` de `float` vers `double` sur ses deux pins compatibles. R28 crée
  `TE_CollapseShared=4.250000`, place son getter et son setter dans le graphe contenu, protège récursivement le
  descripteur encore référencé, puis nettoie et expand le collapse sans divergence. R29 fait passer les graphes
  de 2 à 3, persiste un commentaire ciblé dans `RigVMModel TE_AuxiliaryGraph::`, le retire, puis supprime le modèle
  sous R2 et vérifie le retour à 2. Une régression
  force aussi un apply
  en échec et prouve rollback
  disque + package propre. La baseline Hunter revient à 67 os/
  67 éléments, 0 contrôle, 0 socket, 0 fonction, 0 pin de fonction, 0 nœud, 0 lien et 0 variable sans résidu.
  Rename/remove, dont le retrait d'un getter ou d'un graphe de state reconnu, restent R2 et chaque batch Anim
  Blueprint est compilé avant sauvegarde.
- Niagara : lecture/création System/Emitter/Parameter Collection ; émetteurs, paramètres typés, modules,
  renderers, enable/remove et compilation.
- Audio/MetaSound : création des assets audio courants et MetaSound Source/Patch, Details audio imbriqués et
  authoring interfaces, I/O, variables, nœuds, connexions, defaults et layout MetaSound.
- PCG : inventaire/création Graph et Graph Instance, découverte `UPCGSettings`, inspection bornée des
  nodes/settings/propriétés/pins/arêtes, graph usage et paramètres typés ; patch position/titre/propriété/
  connexion/suppression, schéma de paramètres, valeurs et overrides d'instance avec révision, valeur attendue,
  transaction, R2, sauvegarde et rollback. Les graphes `Asset` sauvegardés disposent aussi de
  plan/start/status/cancel standalone, seed explicite et résumés bornés des données de sortie par classe/pin.
- Project/Validation : état des plugins, changement projet R2, Data Validation et Map Check borné.

La matrice exacte et les limites sont dans
`SourceArt/Docs/29_THOMAS_EDITOR_CAPABILITY_MATRIX.md`. Le terme « AAA » décrit l'architecture et les garde-fous,
pas une prétention de parité totale avec toutes les fenêtres de l'éditeur. Rename/delete/Level Instance Maps,
import/réimport Landscape tuilé ou géospatial et outils érosion/hydro complets, pins dynamiques/subgraphs PCG,
exécution World/Level, preview/debug et bake PCG, 31 gaps AnimGraph mesurés pendant R51 (19 tableaux structurels
top-level encore couplés à des données dérivées/associées ou à un contrat d'asset spécialisé, plus 12 occurrences
de structs complexes), éventuelles références soft/weak si l'inventaire réel en expose, nœuds
non-AnimGraph/Kismet au-delà de R50 et appels Kismet
hors allowlist R38,
structs imbriqués/conteneurs de settings IK Rig/Retargeter, chain mappings et contrôleurs spécialisés de chaque
opération, bindings variables/courbes et batch retarget/export,
bindings d'acteurs
live/sous-séquences/évaluation avancée Sequencer, tracks UMG autres que
`RenderOpacity`, événements multicast, styles complexes/ajout incrémental Widget Tree et
généralisation des jobs async restent
des lots explicites. Le runner HLOD/navigation a son contrat et son smoke réel sur map jetable automatisés ;
progression détaillée et reprise après crash restent à généraliser avant usage production large.

## Configuration du transport

ThomasEditor utilise le serveur MCP natif d'Unreal Engine, limité à la boucle locale. La déclaration Codex reste locale à ce dépôt dans `.codex/config.toml`, qui est ignoré par Git. Un éventuel `.mcp.json` ne configure qu'un autre client MCP : il ne démarre pas le serveur Unreal.

Tant que les correctifs de confinement M1 à M6 ne sont pas fermés et testés, l'autostart reste un choix local et n'est pas versionné. Pour l'activer sur une machine de développement, créer localement `Config/DefaultEditorPerProjectUserSettings.ini` avec le bloc exact suivant :

```ini
[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]
ServerUrlPath=/mcp
ServerPortNumber=8000
bAutoStartServer=True
bEnableToolSearch=True
```

Ce bloc sert aussi de copie de secours après un nettoyage des fichiers non suivis. Le commiter imposerait l'autostart à toute machine ouvrant le projet ; il doit donc rester local jusqu'à la décision explicite post-M1–M6.

Le risque principal n'est pas l'exposition réseau : un client local légitime peut suivre une instruction empoisonnée provenant d'un asset, d'une documentation ou d'un log. La frontière de sécurité est donc le confinement des opérations dans les providers ThomasEditor, pas un jeton de transport.

## Démarrage

1. Relancer Codex depuis la racine PropHunt. `.codex/hooks.json` lance si nécessaire exactement
   `K:\UE58\Engine\Binaries\Win64\UnrealEditor.exe` avec
   `C:\Users\Thomas\Documents\Unreal Projects\PropHunt\PropHunt.uproject`, puis attend le port local.
2. Vérifier l'opt-in local ci-dessus. Pour Codex, la déclaration MCP reste exclusivement dans
   `.codex/config.toml` du projet. D'autres clients peuvent utiliser leur propre configuration, par exemple
   `.mcp.json`; aucun endpoint client ne lance Unreal à lui seul.
3. Chercher le toolset utile, le décrire, puis appeler l'opération. Le Workbench affiche les providers chargés.

Après un redémarrage d'Unreal, un ancien client peut journaliser une fois `Unknown session id`; le serveur répond
alors `client should reinitialize`. Une réinitialisation MCP crée un nouvel identifiant sans redémarrer Codex.

## Preuves au 2026-07-22

- build canonique `PropHuntEditor Win64 Development` avec `K:\UE58` : `Succeeded` ;
- suite `PropHunt.ThomasEditor` : `1/1 Success`, processus `0` ;
- apply Details volontairement non sauvegardé, `SaveAssets`, seconde mutation et seconde sauvegarde validés dans
  la même session ; les packages reviennent propres à chaque étape ;
- Animation Blueprint temporaire créé contre un Skeleton exact, state machine `Locomotion`, états `Idle`/`Run`,
  entrée et transition ajoutés, deux `Sequence Player` reliés et règle constante vraie sauvegardés/rechargés,
  renommage préservant le player, getter direct `CanMove` sauvegardé/rechargé, retrait R2 puis règle constante
  fausse également sauvegardée/rechargée ; `Move` est ensuite converti en `Blend List by Bool` à deux players,
  sauvegardé/rechargé, protégé contre l'écrasement direct et supprimé sous R2 ; un `Blend Space Player` 2D
  `Speed/Direction`, puis un `Blend List by Int` à trois poses `PoseIndex`, subissent le même aller-retour et le
  même nettoyage ; une Sequence `local_space/ref_pose` et son state `Apply Additive` piloté par `AdditiveAlpha`
  sont ensuite sauvegardés/rechargés puis nettoyés sans résidu ; enfin un `Layered Blend per Bone` à deux
  couches, getters `AdditiveAlpha`/`Speed`, branch filters et options mesh-space subit le même cycle complet ;
  la transition `Idle -> Move` reçoit ensuite `Time Remaining Fraction <= 0.20 AND CanMove`, est relue depuis
  disque, protégée contre l'écrasement, puis retirée sous R2 et relue à nouveau ; enfin l'expression typée
  `CanMove AND (Speed > 100 OR NOT(Time Remaining Fraction <= 0.20))` suit le même cycle compile/save/reload,
  reconstruction canonique, refus d'écrasement et retrait/reload R2 ;
- IK Rig temporaire créé contre le Skeletal Mesh exact du Hunter, hiérarchie de 67 os relue depuis disque,
  retarget root/goal/chaîne parent-enfant ajoutés puis sauvegardés/rechargés ; parmi six types disponibles, un
  Limb solver est ajouté/rechargé, configuré start bone/goal/disabled et rechargé, puis déconnecté/supprimé avec
  les autres retraits R2 avant suppression de l'asset sans résidu. Un cycle écrit et recharge aussi root-motion
  bone, os exclu et les dix réglages Limb avec valeurs non défaut. Enfin un Full Body IK est déplacé aller/retour
  dans la pile, reçoit goal et bone setting, puis sauvegarde/recharge `bAllowStretch`, `StrengthAlpha=0.25` et
  `RotationStiffness=0.65` ; un Pole solver valide start/end bone avant nettoyage R2 complet ;
- IK Retargeter temporaire créé depuis cet IK Rig, rechargé après chaque étape structurante, rigs source/cible
  relus, pile par défaut complétée par une opération découverte puis nettoyée sous R2. Deux override sets prouvent
  hiérarchie/set actif et l'override direct `bCopyAllSourceCurves`; deux poses prouvent pose courante et offset Z.
  Une tentative X/Y est refusée avant mutation ; la sauvegarde/relecture disque retrouve exactement les valeurs,
  puis les sets, poses, opérations et deux assets temporaires sont supprimés sans résidu ;
- Control Rig runtime temporaire créé contre `SK_PH_Hunter_Mixamo`; preview mesh et 67 os sont importés, puis un
  null, une courbe, le contrôle float historique et les onze types `ERigControlType` sont ajoutés, sauvegardés/rechargés,
  modifiés et supprimés sous R2. Bool prouve aussi le rename R2 ; forme validée contre la bibliothèque référencée,
  transform de forme et limites Position par canal persistent. Un socket exerce settings/transform/rename/retrait et
  un contrôle exerce deux espaces disponibles, label, ordre et retraits R2. Les vingt types de métadonnées natifs sont
  écrits/rechargés puis retirés. Un apply
  volontairement invalide restaure le fichier et `package_dirty=false`; les sauvegardes suivantes réussissent. Le
  graphe RigVM ajoute `RigUnit_Add_FloatFloat` et `RigUnit_Multiply_FloatFloat`, relit leurs pins `Argument0`,
  `Argument1` et `Result`, écrit `1.25`, déplace le second nœud de `(400,200)` à `(450,250)`, crée puis retire le
  lien `TE_RigVMAdd.Result -> TE_RigVMMultiply.Argument0` et supprime les deux nœuds sous R2 après chaque reload. Un
  commentaire prouve aussi texte/font/bulles, taille, couleur, rename et retrait R2 avec relecture disque exacte.
  R27 crée aussi `TE_RigVMFreeReroute` depuis le pin source `TE_RigVMAdd.Result`, avec type `float`, default
  `3.500000`, widget `TE_TestWidget` et position `(300,100)` ; le nœud et son lien survivent au reload puis sont
  retirés sous R2. Un reroute totalement déconnecté étant éliminé par UE au reload, l'ancrage source ou cible fait
  partie du contrat persistant.
  Un reroute R2 scinde ensuite le lien direct en deux liens via son pin `Value`, est retiré, puis le lien direct est
  restauré ; les trois états sont relus après sauvegarde. R26 collapse ensuite les deux Units avec leur lien sous
  `TE_RigVMCollapse`, relit les quatre nœuds du graphe contenu (Entry/Return inclus), puis expand restaure exactement
  `(100,200)`, `(450,250)`, `Argument0=1.250000` et le lien direct après une nouvelle sauvegarde/relecture.
  Le registre du run R28 expose 753 structs Unit, 9 events, 134 templates, 43 templates dispatch et 0 template
  générique.
  Le smoke crée `ArrayAverage::Execute(in Array,out Average)` par sa notation exacte, refuse un type inconnu puis
  résout son pin wildcard `Array` en `TArray<float>` : le template passe de 4 à 1 permutation et l'état typé exact
  survit à la sauvegarde/relecture. Resize/add/duplicate/remove suit ensuite `0 -> 1 -> 2 -> 3 -> 2 -> 0`, avant
  unresolve R2 vers l'état wildcard initial à 4 permutations. Un vrai `RigVMDispatchNode` `CoreEquals` est aussi
  créé depuis le registre ; résoudre `A` en `float` propage `B`, réduit 6818 permutations à 1, puis unresolve R2
  restaure les wildcards et 6818 permutations. R27 retype d'abord ce dispatch déjà résolu de `float` vers `double`
  sous R2, avec propagation sur `A` et `B` relue après sauvegarde. Les deux nœuds sont enfin supprimés sous R2 et le graphe revient à
  0 nœud/0 lien après reload.
  Un event enregistré `RigUnit_BeginExecution` est ensuite créé comme vrai `RigVMUnitNode` `Forwards Solve`, relu
  après sauvegarde, protégé contre un doublon unique puis supprimé sous R2. Enfin la variable membre float
  `TE_Speed=2.500000` est créée dans le `UControlRigEditorAsset`; ses nœuds getter et setter externes sont
  sauvegardés/rechargés, une réutilisation `int32` incompatible est refusée, puis les deux nœuds et la variable sont
  retirés sous R2. R28 prouve le même accès dans le graphe d'un collapse arbitraire avec
  `TE_CollapseShared=4.250000`; ses getter/setter externes survivent aux reloads, empêchent la suppression du membre
  tant qu'ils existent, puis leur cleanup restaure exactement le collapse avant expansion. Une variable locale y
  est refusée au preflight car UE ne les accepte que dans une graph function. Une fonction locale mutable
  `TE_LocalFunction` est ensuite créée dans la bibliothèque, avec son
  graphe contenu, ses nœuds Entry/Return et son lien ExecuteContext. La variable locale float
  `TE_LocalSpeed=3.500000`, ses getter/setter `Local=true`, puis un vrai `RigVMFunctionReferenceNode` sont chacun
  sauvegardés et rechargés. R24 renomme la variable en `TE_LocalRate`, relit son default `6.250000`, change son type
  en `double` et exerce son index avec une seconde variable. L'interface de la fonction ajoute l'entrée
  `InputScale=1.500000` et la sortie `ResultValue`, puis vérifie sur l'appel la propagation de `InputScale -> Gain`,
  de `ResultValue float -> double` et du retrait de l'entrée. Les doublons, le graphe racine et les suppressions
  encore référencées sont refusés avant mutation ; les suppressions, renames et changements de type sont R2. R25
  publie la fonction, relit category/keywords/description, déplace `ResultValue` de l'index 2 à 1 puis le restaure,
  et propage mutable -> pure -> mutable sur le nœud d'appel sous R2. R29 crée ensuite le modèle top-level
  `RigVMModel TE_AuxiliaryGraph::`, le sauvegarde/recharge, y ajoute et retire un commentaire par son identifiant
  exact, puis le supprime vide sous R2. La surface RigVM atteint cinquante-quatre actions natives.
  R30 inventorie 852 structs et 852 fonctions Unit depuis le registre exact. Le smoke choisit sans chemin codé en
  dur `ControlRigPhysics.RigUnit_HierarchyImportCollisionFromPhysicsAsset::Execute`, rejette une méthode inconnue,
  sauvegarde/recharge la Unit, persiste `BonesToUse` de `0 -> 1 -> 0`, puis la retire sous R2.
  R31 porte la surface à cinquante-six actions. Le smoke découvre `RigVMFunction_MathBoolAnd::Execute`, relit
  `A,B -> Result`, ajoute `C`, sauvegarde/recharge le `URigVMAggregateNode` et son graphe contenu, rejette un état
  périmé, puis retire `C` sous R2 et retrouve exactement le Unit initial `A,B -> Result`.
  La baseline 67 os/0 contrôle/0 null/0 courbe/0 socket est restaurée avant suppression de l'asset, sans fichier
  restant ni fonction/pin de fonction/nœud/lien/variable RigVM. Preuves :
  `Saved/Logs/ThomasEditor_ControlRigAuthoring_R30_DynamicUnit_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterControlRigR30_20260722.log`. Preuves R31 :
  `Saved/Logs/ThomasEditor_ControlRigAuthoring_R31_AggregateProbe7_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterControlRigR31_20260722.log`. R32 valide en plus
  `TransformNoScale`, `EulerTransform` et `ScaleFloat`, leurs valeurs natives après save/reload, le refus d'une
  échelle non unitaire masquée et le nettoyage R2 ; preuves :
  `Saved/Logs/ThomasEditor_ControlRigAuthoring_R32_SpecializedControls_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterControlRigR32_20260722.log`. R33 valide la bibliothèque référencée et ses
  55 formes, le refus d'un nom absent, les flags de limites `X,Y,Z` distincts et les vingt types de métadonnées avec
  save/reload/suppression R2 ; preuves :
  `Saved/Logs/ThomasEditor_ControlRigAuthoring_R33_ShapesLimitsMetadata_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterControlRigR33_20260722.log`. R34 renomme puis supprime directement le graphe
  top-level peuplé, exerce une fonction publique dans un second Control Rig, protège sa suppression tant qu'elle est
  référencée, puis nettoie les deux assets sans résidu ; preuves :
  `Saved/Logs/ThomasEditor_ControlRigAuthoring_R34_ExternalLibrariesGraphLifecycle_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterControlRigR34_20260722.log` ;
- R35 crée `Local Ref Pose` et `Rotate Root Bone` dans le state `Move`, écrit `Pitch=15.0`, raccorde les deux
  poses jusqu'au `State Result`, recharge chaque mutation, refuse classe protégée et hash périmé, prouve rollback
  exact sur default invalide, puis retire liens/nœuds sous R2 et restaure le `ContentHash` initial sans asset
  résiduel ; preuves : `Saved/Logs/ThomasEditor_AnimationStateGraphGeneric_R35_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterAnimationR35_20260722.log` ;
- R36 inspecte puis inverse/revient sur `Node.bRotateRootMotionAttribute` du `Rotate Root Bone` avec
  `PropertyCount/PropertyHash`, compile/save/reload chaque état, refuse le champ déjà exposé en pin `Node.Pitch`,
  le struct imbriqué non allowlisté `Node.PitchScaleBiasClamp`, une valeur bool invalide, un no-op et un hash
  périmé, puis retrouve le hash exact de R35 sans asset résiduel ; preuves :
  `Saved/Logs/ThomasEditor_AnimationStateGraphProperties_R36_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterAnimationR36_20260722.log` ;
- R37 compile/sauvegarde/recharge une expression RPN de 32 tokens qui exerce les huit getters non arbitraires,
  `add/subtract/multiply/divide/min/max/abs`, les six comparaisons et la logique booléenne, puis reconstruit
  exactement 28 nœuds de règle. Le preflight refuse le getter de poids d'un state arbitraire sans contexte,
  `abs` sur un bool et l'appel non allowlisté `sqrt`; le retrait R2 restaure la baseline sans asset résiduel ;
  preuves : `Saved/Logs/ThomasEditor_AnimationTransitionExpressions_R37_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterAnimationR37_20260722.log` ;
- R38 porte la même expression à 36 tokens/31 nœuds avec
  `arbitrary_state_blend_weight:Move`. Le preflight refuse contexte manquant, état inconnu et contexte sur un autre
  getter ; `Move -> MotionR38 -> Move` est sauvegardé/rechargé à chaque rename et le canon suit exactement le
  pointeur `AssociatedStateNode`, avant retrait R2 et nettoyage sans asset résiduel ; preuves :
  `Saved/Logs/ThomasEditor_AnimationTransitionArbitraryState_R38_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterAnimationR38_20260722.log` ;
- R39 inspecte/mute/recharge `Node.PitchScaleBiasClamp.Scale`,
  `Node.LayerSetup[0].BranchFilters[0].BlendDepth` et la référence forte `Node.Sequence`. Le smoke refuse le parent
  struct complet, un entier invalide, l'index 16, un asset hors `/Game/PropHunt` et une classe incompatible ; il
  restaure chaque hash exact, compile sans erreur/warning et nettoie les dix assets Animation temporaires via Unreal.
  Preuves : `Saved/Logs/ThomasEditor_AnimationStateGraphComplexProperties_R39_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterAnimationR39_20260722.log` ;
- R40 inspecte/hash les conteneurs, refuse resize sans R2, taille 17, no-op et `Node.LayerSetup` structurel, puis
  persiste `Node.LayerSetup[0].BranchFilters` de `1 -> 2`, écrit/recharge `BranchFilters[1].BlendDepth=4` et revient
  à `2 -> 1` avec le hash exact. Les trois applies compilent/sauvegardent sans erreur/warning et le cleanup reste
  sans résidu ; preuves : `Saved/Logs/ThomasEditor_AnimationStateGraphArrayLifecycle_R40_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterAnimationR40_20260722.log` ;
- R41 inventorie les kinds AnimGraph encore hors surface puis retire les `TMap`/`TSet` scalaires de cette liste.
  Le smoke persiste/recharge deux entrées de `ModifyCurve.Node.CurveMap` et de `RemoveCurve.Node.Curves`, refuse
  absence de R2, texte invalide, 17 entrées, no-op et hash périmé, restaure les conteneurs et le hash exact puis
  supprime les deux nœuds sans résidu ; preuves :
  `Saved/Logs/ThomasEditor_AnimationStateGraphAssociativeContainers_R41_Final_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterAnimationR41_20260722.log` ;
- R42 retire les deux gaps `class` de l'inventaire, crée un second Anim Blueprint et persiste sa classe générée
  exacte dans `LinkedAnimGraph.Node.InstanceClass`. Le smoke refuse R2 absent, package path sans `_C`, asset
  incompatible, classe hors PropHunt, no-op et hash périmé, puis restaure `None`, retire le nœud et supprime les
  onze assets Animation temporaires sans résidu ; preuves :
  `Saved/Logs/ThomasEditor_AnimationLinkedAnimInstanceClass_R42_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterAnimationR42_20260722.log` ;
- R43 retire tous les `TObjectPtr<UCurveFloat>` des gaps `object`, crée/sauvegarde une courbe à deux clés et la
  persiste/recharge dans `BlendListByBool.Node.CustomBlendCurve`. Le smoke refuse asset hors PropHunt, classe
  incompatible, no-op et hash périmé, restaure `None`, supprime le nœud sous R2 et nettoie les douze assets
  Animation temporaires ; preuves : `Saved/Logs/ThomasEditor_AnimationCurveFloatReferences_R43_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterAnimationR43_20260722.log` ;
- R44 retire des gaps `object` les `TObjectPtr<UPhysicsAsset>` et `TObjectPtr<UMirrorDataTable>`. Le smoke persiste
  un Physics Asset sur `RigidBody.Node.OverridePhysicsAsset` puis une Mirror Data Table valide sur
  `Mirror.Node.MirrorDataTable`, couvre refus path/classe/no-op/hash, restaure `None`, retire les deux nœuds sous R2
  et nettoie les quatorze assets Animation temporaires ; preuves :
  `Saved/Logs/ThomasEditor_AnimationPhysicsMirrorReferences_R44_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterAnimationR44_20260722.log` ;
- R45 retire les neuf derniers gaps `object` forts : six Blend Profiles, IK Rig, IK Retargeter et Physics Control
  Asset. Le Blend Profile est résolu comme sous-objet exact d'un Skeleton temporaire ; IK/Retargeter exigent R2,
  les deux autres affectations restent R1. Les quatre cycles couvrent refus path/classe/no-op/hash, save/reload,
  restauration et hash exact, puis nettoient seize assets Animation temporaires ; preuves :
  `Saved/Logs/ThomasEditor_AnimationStrongObjectReferences_R45_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterAnimationR45_20260722.log` ;
- R46 retire 41 gaps top-level de structs AnimGraph et expose leurs feuilles imbriquées éditables, y compris
  `SocketReference`, sans exposer caches transitoires ni pose links. Les cycles Sequence Player/LookAt/
  TwistCorrective couvrent invalid/no-op/hash périmé, R1, reconstruction Editor fidèle, save/reload/restauration et
  hash exact ; quatorze applies ont `compile_errors=0 compile_warnings=0`, zéro résidu ; preuves :
  `Saved/Logs/ThomasEditor_AnimationNestedSettings_R46_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterAnimationR46_20260722.log` ;
- R47 classe séparément 57 liens de pose (`34 FPoseLink + 23 FComponentSpacePoseLink`) et ferme sept occurrences
  de réglages via six structs supplémentaires. Le backlog réfléchi tombe de 119 à 55 vrais gaps, dont 43
  `structural_array`; six cycles sur cinq classes couvrent refus, R1, save/reload/restauration/hash exact et 22
  applies à `compile_errors=0 compile_warnings=0`, zéro résidu ; preuves :
  `Saved/Logs/ThomasEditor_AnimationSettingsAndTopology_R47_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterAnimationR47_20260722.log` ;
- R48 retire dix tableaux autonomes des gaps et les expose comme remplacements complets R2 bornés à 16. Les
  tableaux d'os valident chaque nom contre le Skeleton, les tableaux vides s'exportent en `()`, et l'inventaire
  passe à `StateGraphWholeArrayPropertyCount=10`, `StateGraphPropertyGapCount=45` et
  `StateGraphPropertyGap.structural_array=33`. Quatre cycles sur Inertialization/PoseDriver produisent 12 applies
  à `compile_errors=0 compile_warnings=0`, restauration/hash exact et zéro résidu ; preuves :
  `Saved/Logs/ThomasEditor_AnimationWholeArrays_R48_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterAnimationR48_20260722.log` ;
- R49 classe cinq tableaux de `FPoseLink` comme topologie et rattache sept tableaux aux actions dédiées Bool
  Blend/Int Blend/Layered Blend per Bone, soit neuf fermetures uniques. Les métriques deviennent
  `TopologyProperty.pose_link_array=5`, `DedicatedArrayCount=7`, `ResolvedStructuralArrayCount=19`,
  `PropertyGapCount=36` et `structural_array=24`; build, test ciblé, suite complète et zéro résidu sont verts.
  Les deux topologies dynamiques Enum/MultiWay restent nommées pour le prochain lifecycle de pins ; preuves :
  `Saved/Logs/ThomasEditor_AnimationManagedArrays_R49_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterAnimationR49_20260722.log` ;
- R50 ajoute `resize_state_graph_pose_array` sous R2 pour `BlendListByEnum` et `MultiWayBlend`. L'inspection relit
  pose/compagnon, tailles, valeurs, entrées d'enum canoniques et nombres de pins ; le preflight exige une enum
  exacte à la création, une entrée unique par pose non-default, 2..16 poses et respectivement des temps 0..60 ou
  alphas 0..1. Les deux cycles 2 -> 3 -> 2 restaurent le hash exact,
  suppriment les pins orphelins et produisent dix applies sans erreur/warning. Les métriques deviennent
  `CoupledPoseArrayLifecycleCount=2`, `CoupledPoseArrayPropertyCount=4`,
  `ResolvedStructuralArrayCount=21`, `PropertyGapCount=34` et `structural_array=22`; preuves :
  `Saved/Logs/ThomasEditor_AnimationCoupledPoseArrays_R50_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterAnimationR50_20260722.log` ;
- R51 inventorie chaque gap avec son `InnerType` puis ferme le premier tableau asset-bound,
  `RetargetPoseFromMesh.Node.OverrideSetsToApply`. Les noms sont uniques et validés contre les override sets de
  l'IK Retargeter assigné ; remplacement/restauration R2, save/reload et nettoyage ramènent le hash exact. Les
  métriques deviennent `AssetBoundArrayLifecycleCount=1`, `AssetBoundArrayPropertyCount=1`,
  `ResolvedStructuralArrayCount=22`, `PropertyGapCount=33` et `structural_array=21`; preuves :
  `Saved/Logs/ThomasEditor_AnimationAssetBoundOverrideSets_R51_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterAnimationR51_20260722.log` ;
- R51 lot 2 ajoute `replace_state_graph_constraint_array` pour le couple atomique
  `ConstraintSetup/ConstraintWeights` : `FConstraint` 0..16, os présents dans le Skeleton, poids parallèles
  `[0,1]`, préflight sur copie, R2, relecture canonique et nettoyage des pins. Le cycle `0 -> 1 -> 2 -> 0`
  sauvegarde/recharge et restaure le hash exact ; métriques `CoupledConstraintArrayLifecycleCount=1`,
  `CoupledConstraintArrayPropertyCount=2`, `ResolvedStructuralArrayCount=24`, `PropertyGapCount=31` et
  `structural_array=19`; preuves :
  `Saved/Logs/ThomasEditor_AnimationConstraintArrays_R51Lot2Fix_20260722.log` et
  `Saved/Logs/ThomasEditor_FullSuiteAfterAnimationR51Lot2_20260722.log` ;
- PCG Graph Instance créé contre un Graph `Asset`, paramètre `double` ajouté/renommé, override appliqué puis
  réinitialisé, exécution standalone terminée avec `PCGParamData` sur `Out`, cancel post-completion refusé et
  Graph/Instance supprimés sans résidu ;
- Level Sequence temporaire enrichie d'un `CineCameraActor` spawnable configuré, d'un Camera Cut lié à son GUID
  et d'un event `TE_OnCameraCut` compilé dans son Director Blueprint ; inspection aller/retour, gardes R2,
  nettoyage des endpoints/pistes/binding et suppression de l'asset validés sans résidu ;
- Widget Blueprint temporaire compilé/sauvegardé avec animation `Pulse` à 60 fps, deux clés `RenderOpacity` sur
  `TestLabel` et binding de propriété `TestButton.bIsEnabled <- IsButtonEnabled`; inspection exacte, refus sans
  confirmation, suppressions R2 et zéro résidu validés ;
- exactement trois méta-tools live, projet `PropHunt`, moteur `5.8.0-0+UE5` ;
- au premier appel Animation live, seuls `ThomasEditorAssets` et `ThomasEditorAnimation` passent chargés ;
- 54 assets Animation/Character trouvés et `A_PH_Hunter_Idle` relu : skeleton, 1,9 s, 115 samples à 60 fps ;
- preuves temporaires créées/sauvées/relues/supprimées pour Blueprint/UMG, Material, Decal, Post Process,
  Enhanced Input, Data/AI, Level Sequence, Paper2D, Audio, MetaSound, Niagara, Anim Montage, Foliage, Data Layers,
  lifecycle Maps, Landscape 64x64 avec edit layers, lecture/hash height/weight, régions height, import R16, sculpt,
  création LayerInfo/material target, paint et import R8, ainsi que PCG Graph/Graph Instance, paramètres,
  overrides et exécution standalone ;
- smoke lourd World Partition : create/reload, navigation/build HLOD/delete HLOD, trois processus `0`, zéro asset ;
- aucun `.uasset` temporaire restant sous `/Game/PropHunt/Tests/ThomasEditor` ;
- `M_PH_InteractableOutline_PP_V2` audité sans mutation : 72 expressions, 55 connexions, 42 atteignables,
  30 inatteignables, 27 orphelines directes et 21 groupes superposés.

Logs principaux : `Saved/Logs/PropHunt-backup-2026.07.20-15.07.34.log`,
`Saved/Logs/ThomasEditor_FullSuite.log`, `Saved/Logs/ThomasEditor_FoliageDeep4.log`,
`Saved/Logs/ThomasEditor_DataLayersDeep2.log`, `Saved/Logs/ThomasEditor_MapLifecycle.log` et
`Saved/Logs/ThomasEditor_LandscapeDeep4.log`, `Saved/Logs/ThomasEditor_NavigationHLODDeep.log` et
`Saved/Logs/ThomasEditor_PCGDeep.log`, `Saved/Logs/ThomasEditor_FullSuiteAfterPCGWP.log`,
`Saved/Logs/ThomasEditor_LandscapeDataDeep6.log`, `Saved/Logs/ThomasEditor_FullSuiteAfterLandscapeData.log`, puis
`Saved/Logs/ThomasEditor_EnvironmentBuilderSmoke2.log` et ses trois
logs enfants sous `Saved/Logs/ThomasEditor/EnvironmentJobs`, ainsi que
`Saved/Logs/ThomasEditor_AnimationSaveStateMachine_20260721.log` et
`Saved/Logs/ThomasEditor_AnimationStateContent_R2_20260721.log`, puis
`Saved/Logs/ThomasEditor_AnimationVariableRules_20260721.log` et
`Saved/Logs/ThomasEditor_AnimationBoolBlend_20260721.log`, puis
`Saved/Logs/ThomasEditor_AnimationBlendSpacePlayer_20260721.log` et
`Saved/Logs/ThomasEditor_AnimationIntBlend_20260721.log`, puis
`Saved/Logs/ThomasEditor_AnimationApplyAdditive_20260721.log`, puis
`Saved/Logs/ThomasEditor_AnimationLayeredBlend_20260721.log` et
`Saved/Logs/ThomasEditor_AnimationTransitionGetter_20260721.log`, puis
`Saved/Logs/ThomasEditor_AnimationTransitionExpression_20260721.log`, puis
`Saved/Logs/ThomasEditor_AnimationIKRigAuthoring_20260721.log` et
`Saved/Logs/ThomasEditor_AnimationIKRigSolvers_20260721.log`, puis
`Saved/Logs/ThomasEditor_AnimationIKRigLimbSettings_20260721.log`, puis
`Saved/Logs/ThomasEditor_AnimationIKRigGenericSettings_20260721.log`, puis
`Saved/Logs/ThomasEditor_AnimationIKRetargeter_20260721.log`, puis
`Saved/Logs/ThomasEditor_AnimationControlRigControls_20260721.log`.
La preuve Cinematics avancée est dans
`Saved/Logs/ThomasEditor_CinematicsAdvanced_20260721.log`.
La preuve PCG Graph Instance/exécution est dans
`Saved/Logs/ThomasEditor_PCGInstancesExecution2_20260721.log`.
La preuve animation/binding UMG est dans
`Saved/Logs/ThomasEditor_UMGAdvanced_R3_20260721.log`.
