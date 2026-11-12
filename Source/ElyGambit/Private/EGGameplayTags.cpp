#include "EGGameplayTags.h"

namespace EGTags
{
	UE_DEFINE_GAMEPLAY_TAG(Damage, "AutoChess.Data.Damage");
 UE_DEFINE_GAMEPLAY_TAG(DamageExecution, "AutoChess.Damage.Execution");
	UE_DEFINE_GAMEPLAY_TAG(Mana, "AutoChess.Data.Mana");
	UE_DEFINE_GAMEPLAY_TAG(BasicAttack, "AutoChess.Ability.BasicAttack");
	UE_DEFINE_GAMEPLAY_TAG(Ultimate, "AutoChess.Ability.Ultimate");
	UE_DEFINE_GAMEPLAY_TAG(Dead, "AutoChess.State.Dead");
	UE_DEFINE_GAMEPLAY_TAG(Combat, "AutoChess.State.Combat");
	UE_DEFINE_GAMEPLAY_TAG(Acting, "AutoChess.State.Acting");
	UE_DEFINE_GAMEPLAY_TAG(Casting, "AutoChess.State.Casting");
 UE_DEFINE_GAMEPLAY_TAG(TeamAttackSpeed, "AutoChess.Data.TeamAttackSpeed");
 UE_DEFINE_GAMEPLAY_TAG(ProtectionReduction, "AutoChess.Data.ProtectionReduction");
 UE_DEFINE_GAMEPLAY_TAG(Control, "AutoChess.Control");
 UE_DEFINE_GAMEPLAY_TAG(ControlStun, "AutoChess.Control.Stun");
 UE_DEFINE_GAMEPLAY_TAG(ControlFreeze, "AutoChess.Control.Freeze");
 UE_DEFINE_GAMEPLAY_TAG(ControlSilence, "AutoChess.Control.Silence");
 UE_DEFINE_GAMEPLAY_TAG(ControlForcedMovement, "AutoChess.Control.ForcedMovement");
} // namespace EGTags
