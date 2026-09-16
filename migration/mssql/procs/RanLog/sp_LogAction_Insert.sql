

CREATE PROCEDURE [dbo].[sp_LogAction_Insert]
	@nChaNum int,
	@nType int,
    @nTargetNum int,
    @nTargetType int,
    @nExpPoint money,
    @nBrightPoint int,
    @nLifePoint int,
    @nMoney money
AS	
	SET NOCOUNT ON
	IF (@nType=2)
	BEGIN
	UPDATE PhindoGame.dbo.ChaInfo
	SET ChaKills=ChaKills+1
	WHERE ChaNum = @nChaNum
	END
	INSERT INTO LogAction (ChaNum,   Type,   TargetNum,   TargetType,   ExpPoint,   BrightPoint,   LifePoint,   ActionMoney) 
    VALUES    (@nChaNum, @nType, @nTargetNum, @nTargetType, @nExpPoint, @nBrightPoint, @nLifePoint, @nMoney)

	SET NOCOUNT OFF


