# Règles de travail PropHunt

Ces règles s'appliquent à tout le dépôt.

## Lecture obligatoire au début de chaque tâche

Avant toute commande, modification, compilation ou ouverture d'Unreal :

1. Lire ce fichier `AGENTS.md`, en particulier **Projet et moteur canoniques**.
2. Lire `SourceArt/Docs/PROJECT_STATUS.md`, puis le document du système concerné.
3. Pour reprendre la Phase 1, lire `SourceArt/Docs/08_PHASE1_HANDOFF.md`.
4. Vérifier dans la commande prévue que le projet est exactement
   `C:\Users\Thomas\Documents\Unreal Projects\PropHunt\PropHunt.uproject` et le moteur `K:\UE58`.

Ne jamais déduire le projet cible depuis les projets récents d'Unreal, le nom du dossier parent, une autre
conversation ou un autre checkout. En cas de contradiction entre une note ancienne et les chemins canoniques
ci-dessous, les chemins canoniques de ce fichier gagnent et la documentation obsolète doit être corrigée avant
de continuer.

## Projet et moteur canoniques

- Ce dépôt est **PropHunt**, distinct de `JeuMulti58` et de tout projet d'outils MCP.
- Le projet à ouvrir est exclusivement :
  `C:\Users\Thomas\Documents\Unreal Projects\PropHunt\PropHunt.uproject`.
- Le moteur canonique est le checkout source UE 5.8 : `K:\UE58`.
- Ne jamais lancer `UnrealEditor.exe` seul. Ouvrir directement le `.uproject` ci-dessus ou le passer comme
  premier argument complet à `K:\UE58\Engine\Binaries\Win64\UnrealEditor.exe`.
- Commande PowerShell canonique :
  `& 'K:\UE58\Engine\Binaries\Win64\UnrealEditor.exe' 'C:\Users\Thomas\Documents\Unreal Projects\PropHunt\PropHunt.uproject'`.
- Avant tout build, cook, test ou lancement, vérifier que le projet ciblé est bien `PropHunt.uproject`.
- Ne jamais lire, déplacer ou modifier des fichiers dans `JeuMulti58`, `PluginMCP`, un autre plugin ou le
  moteur pour accomplir une tâche PropHunt, sauf demande explicite et séparée du propriétaire.

## Avant toute modification

- Si le dépôt interne `SourceArt` est présent localement, lire `SourceArt/Docs/PROJECT_STATUS.md` et les documents liés au système concerné. S'il est absent, ne pas inventer une décision de conception non visible dans le dépôt public.
- Préserver les modifications existantes qui ne concernent pas la tâche.
- Ne jamais supposer qu'une décision marquée `À tester` est validée.
- Ne pas ajouter de plugin tiers sans validation et sans consigner le besoin, la licence, la maintenance, le coût réseau et l'alternative native dans la documentation interne.

## Unreal

- C++ porte l'autorité, les règles, la réplication et les validations.
- Blueprint porte la présentation et la configuration exposée ; éviter les règles critiques uniquement dans un Blueprint.
- Tous les appels client qui modifient le jeu sont validés par le serveur.
- Aucun asset de production ne référence `/Game/Developers`.
- Ne jamais déplacer, renommer ou supprimer un `.uasset` ou `.umap` directement dans l'explorateur ; utiliser l'éditeur Unreal et corriger les redirectors.
- Ne pas générer de contenu procédural au runtime. La sélection serveur d'éléments préfabriqués et cuisinés reste autorisée.
- Tester au minimum en listen server avec un client distant avant de déclarer un système multijoueur terminé.

## Outils Editor et plugins de liaison

- La déclaration MCP Codex de `ThomasEditor` reste uniquement dans `.codex/config.toml` de ce dépôt. Ne pas
  l'ajouter à la configuration globale `C:\Users\Thomas\.codex\config.toml`.
- Commencer par les capacités natives UE 5.8 : interface de l'éditeur, Class Settings, Class Defaults,
  Remote Control, PythonScriptPlugin et APIs Editor exposées.
- Si un pont supplémentaire est nécessaire, créer sa logique depuis zéro uniquement sous
  `Plugins/ThomasEditor`. Ne jamais copier, déplacer ou modifier un autre plugin pour l'obtenir.
- `ThomasEditor` reste Editor-only : aucun module runtime, aucune règle de jeu, aucune réplication et aucun
  asset de production.
- Le plugin doit exposer une surface minimale, des réponses compactes, des limites explicites et des
  garde-fous avant toute mutation. Compiler et sauvegarder seulement après validation.
- Remote Control doit autoriser uniquement les fonctions nécessaires. Ne jamais activer globalement
  `Allow Any Remote Function Call` et ne pas ajouter un second serveur réseau sans besoin mesuré.
- Aucun secret permanent dans le dépôt. Les tokens de session et caches restent sous `Saved/` ou dans une
  configuration locale ignorée.
- Documenter dans `SourceArt/Docs` le besoin, la surface, la sécurité, la maintenance, le coût réseau,
  l'alternative native et les tests de tout plugin de liaison.

## Qualité

- Respecter les conventions internes lorsqu'elles sont disponibles dans le checkout privé `SourceArt`.
- Une fonctionnalité n'est terminée que si ses cas de déconnexion et d'autorité sont traités.
- Les valeurs d'équilibrage ne sont pas codées en dur : elles viennent d'une configuration C++ exposée ou d'un Data Asset.
- Mettre à jour le statut et le changelog internes après une étape validée.
