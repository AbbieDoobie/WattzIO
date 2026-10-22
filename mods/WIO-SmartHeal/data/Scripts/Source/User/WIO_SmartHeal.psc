ScriptName WIO_SmartHeal Hidden Const
{Smart Heal Button's bridge to Actor.EquipItem. Called by WIO-SmartHeal.dll; attached to nothing.}

; Uses an aid item on the player through Papyrus, which is the route that plays the use animation.
;
; It takes a FormID rather than the item because a C++ caller cannot pass a Potion where EquipItem
; declares a Form: the VM rejects it with "Type mismatch for argument 1". Inside a script the widening
; is ordinary, so the item is looked up and passed on here.
;
; Global, so it needs no plugin: the VM loads this script by name the first time it is called.
Function UseItem(int aiFormID) global
	Form akItem = Game.GetForm(aiFormID)
	If akItem
		; abPreventRemoval false because the item is being used; abSilent true because the plugin
		; prints its own summary.
		Game.GetPlayer().EquipItem(akItem, false, true)
	EndIf
EndFunction
