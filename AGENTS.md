# Règles de travail PropHunt

Ces règles s'appliquent à tout le dépôt.

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

## Qualité

- Respecter les conventions internes lorsqu'elles sont disponibles dans le checkout privé `SourceArt`.
- Une fonctionnalité n'est terminée que si ses cas de déconnexion et d'autorité sont traités.
- Les valeurs d'équilibrage ne sont pas codées en dur : elles viennent d'une configuration C++ exposée ou d'un Data Asset.
- Mettre à jour le statut et le changelog internes après une étape validée.
