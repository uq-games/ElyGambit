#include "Data/EGStigmataDefinition.h"
#include "Data/EGStigmataBalance.h"
#include "Engine/Texture2D.h"

const TArray<FEGStigmataDefinition>& UEGStigmataCatalog::GetDefinitions()
{
    static const TArray<FEGStigmataDefinition> Definitions = [] {
        const auto& B=UEGStigmataBalanceSettings::Get();
        auto P=[](float Value){return FMath::RoundToInt(Value*100.f);};
        TArray<FEGStigmataDefinition> Result;
        auto Add = [&Result](const TCHAR* Id, const TCHAR* Name, const TCHAR* Category, const TCHAR* Description)
        {
            FEGStigmataDefinition& Entry = Result.AddDefaulted_GetRef();
            Entry.Id = FName(Id);
            Entry.Name = FText::FromString(Name);
            Entry.Category = FText::FromString(Category);
            Entry.Description = FText::FromString(Description);
        };
        Add(TEXT("LoveBeginning"),TEXT("爱之伊始"),TEXT("刻印  爱莉"),*FString::Printf(TEXT("英桀攻速提高%d%%；普通爱莉希雅与人之律者额外提高%d%%。"),P(B.LoveTeamSpeed),P(B.LoveSpeed)));
        Add(TEXT("Flawless"),TEXT("无瑕之人"),TEXT("刻印  爱莉"),*FString::Printf(TEXT("英桀技能效果提高%d%%；普通爱莉强化射击延长%g秒，人律终结伤害额外提高%d%%。"),P(B.FlawlessSkillBonus),B.FlawlessSeconds,P(B.FlawlessFinisherMultiplier-1.f)));
        Add(TEXT("OriginFlower"),TEXT("光辉矢愿"),TEXT("终局刻印"),TEXT("所有上阵英桀离场，获得一张人之律者，其等级为离场英桀等级总和的2倍。"));
        Add(TEXT("Story"),TEXT("因你而在的故事"),TEXT("终局刻印"),*FString::Printf(TEXT("上阵上限提高至9，获得九名英桀各一张赠卡；持有英桀与手牌等级最低提升至%d级，升变权限保留。赠卡成本为零。"),B.StoryMinimumLevel));
        Add(TEXT("Unity"),TEXT("英桀同心"),TEXT("终局刻印"),*FString::Printf(TEXT("保留阵容与人口；我方英桀、召唤物及机兵伤害提高%d%%，最大生命提高%d%%。派生属性不重复加成。"),P(B.UnityDamageBonus),P(B.UnityHealthBonus)));
        Add(TEXT("Deliverance"),TEXT("救世"),TEXT("刻印  凯文"),*FString::Printf(TEXT("英桀必杀后自身伤害提高%d%%（%g秒，含本次必杀与刻印追加）；重复触发刷新，不叠层。"),P(B.DeliveranceBonus),B.DeliveranceDuration));
        Add(TEXT("Discipline"),TEXT("戒律"),TEXT("刻印  阿波尼亚"),*FString::Printf(TEXT("英桀连续普攻同一主目标：第二次起普攻伤害提高%d%%、%d%%、%d%%，此后保持最高值；更换主目标重计。"),P(B.DisciplineStep),P(B.DisciplineStep*2),P(B.DisciplineStep*3)));
        Add(TEXT("Gold"),TEXT("黄金"),TEXT("刻印  伊甸"),*FString::Printf(TEXT("英桀开战额外获得%d%%最大法力；必杀后返还消耗法力的%d%%。人律返蓝于回弓后兑现，不因转形清零。"),P(B.GoldOpeningMana),P(B.GoldRefund)));
        Add(TEXT("Helix"),TEXT("螺旋"),TEXT("刻印  维尔薇"),*FString::Printf(TEXT("必杀后储存%d次炮击；后续有效普攻各在主目标造成%d%%攻击力、%g格半径范围伤害，再次必杀刷新次数。炮击不算普攻、不回蓝、不递归。"),B.HelixCharges,P(B.HelixDamage),B.HelixRadiusCells));
        Add(TEXT("Decimation"),TEXT("鏖灭"),TEXT("刻印  千劫"),*FString::Printf(TEXT("英桀每损失1%%生命提高%g%%伤害，最高%d%%。直接伤害获得%d%%吸血；追加伤害与持续伤害不触发吸血。"),B.DecimationMissingHealthScale,P(B.DecimationMaxBonus),P(B.DecimationLifesteal)));
        Add(TEXT("Wisdom"),TEXT("天慧"),TEXT("刻印  苏"),*FString::Printf(TEXT("全队每%d次有效普攻，在命中处触发%d%%存活上阵英桀平均攻击力、%g格半径冲击；一次普攻行为最多计一次，召唤物与追加伤害不计数。"),B.WisdomAttacks,P(B.WisdomDamage),B.WisdomRadiusCells));
        Add(TEXT("Setsuna"),TEXT("刹那"),TEXT("刻印  樱"),*FString::Printf(TEXT("英桀自动闪避下一次敌方直接攻击，随后攻速提高%d%%（%g秒）；每人冷却%g秒。持续伤害与自身扣血不触发。"),P(B.SetsunaSpeed),B.SetsunaSpeedDuration,B.SetsunaCooldown));
        Add(TEXT("Daybreak"),TEXT("旭光"),TEXT("刻印  科斯魔"),*FString::Printf(TEXT("英桀普攻主目标施加%g秒撕裂，每秒造成%d%%攻击力伤害；同一施加者刷新，不同施加者独立计算；对护盾伤害提高%d%%。"),B.DaybreakDuration,P(B.DaybreakDamage),P(B.DaybreakShieldBonus)));
        Add(TEXT("Infinity"),TEXT("无限"),TEXT("刻印  梅比乌斯"),*FString::Printf(TEXT("我方英桀、召唤物及机兵最大生命与攻击力提高%d%%；每项属性只加成一次，派生属性不再次放大。"),P(B.InfinityBonus)));
        Add(TEXT("Stars"),TEXT("繁星"),TEXT("刻印  格蕾修"),*FString::Printf(TEXT("本局持有与之后获得的格蕾修升变为天行格蕾修，技能效果额外提高%d%%。保留等级、合成进度、觉醒、永久成长与身份。"),P(B.FormSkillBonus)));
        Add(TEXT("Vicissitude"),TEXT("浮生"),TEXT("刻印  华"),*FString::Printf(TEXT("本局持有与之后获得的华升变为崩落华，技能效果额外提高%d%%。保留等级、合成进度、觉醒、永久成长与身份。"),P(B.FormSkillBonus)));
        Add(TEXT("Reverie"),TEXT("空梦"),TEXT("刻印  帕朵菲莉丝"),*FString::Printf(TEXT("立即获得%d崩坏能；后续每个准备阶段额外获得%d次免费刷新，未用次数不累积，重试不重置。"),B.ReverieGold,B.ReverieExtraFreeRefreshes));
        Add(TEXT("DuskStreet"),TEXT("黄昏街"),TEXT("羁绊"),*FString::Printf(TEXT("帕朵菲莉丝、樱、阿波尼亚同时上阵：开战时我方单位获得%d%%最大生命护盾，本关首次有效结算额外获得%d崩坏能。"),P(B.DuskShield),B.DuskGold));
        Add(TEXT("Kindred"),TEXT("同类"),TEXT("羁绊"),*FString::Printf(TEXT("维尔薇、梅比乌斯同时上阵：维尔薇必杀首次直接命中后追加%d%%攻击力炮击，优先最低生命比例连接目标，无连接时攻击主目标；不算普攻、不递归、不触发传导。"),P(B.KindredDamage)));
        Add(TEXT("LastBanquet"),TEXT("最后的宴会"),TEXT("羁绊"),*FString::Printf(TEXT("阿波尼亚、伊甸、爱莉同时上阵：任一成员必杀后为另两名存活成员恢复%d%%最大法力，全组冷却%g秒。"),P(B.BanquetMana),B.BanquetCooldown));
        const TMap<FName,FString> ImagePaths{
            {TEXT("LoveBeginning"),TEXT("/Game/Assets/Cards/Elysia/T_Card_Elysia.T_Card_Elysia")},
            {TEXT("Flawless"),TEXT("/Game/Assets/Cards/Elysia/T_Card_Elysia.T_Card_Elysia")},
            {TEXT("Discipline"),TEXT("/Game/Assets/Cards/Aponia/T_Card_Aponia.T_Card_Aponia")},
            {TEXT("Gold"),TEXT("/Game/Assets/Cards/Eden/T_Card_Eden.T_Card_Eden")},
            {TEXT("Helix"),TEXT("/Game/Assets/Cards/VillV/T_Card_VillV.T_Card_VillV")},
            {TEXT("Setsuna"),TEXT("/Game/Assets/Cards/Sakura/T_Card_Sakura.T_Card_Sakura")},
            {TEXT("Infinity"),TEXT("/Game/Assets/Cards/Mobius/T_Card_Mobius.T_Card_Mobius")},
            {TEXT("Stars"),TEXT("/Game/Assets/Cards/Griseo/T_Card_Griseo.T_Card_Griseo")},
            {TEXT("Vicissitude"),TEXT("/Game/Assets/Cards/FuHua/T_Card_Hua.T_Card_Hua")},
            {TEXT("Reverie"),TEXT("/Game/Assets/Cards/Pardofelis/T_Card_Pardo.T_Card_Pardo")}
        };
        for(FEGStigmataDefinition& Entry:Result)
            if(const FString* Path=ImagePaths.Find(Entry.Id))
                Entry.Image=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(*Path));
        return Result;
    }();
    return Definitions;
}

const FEGStigmataDefinition* UEGStigmataCatalog::Find(FName Id)
{
    return GetDefinitions().FindByPredicate([Id](const FEGStigmataDefinition& Definition){return Definition.Id == Id;});
}
