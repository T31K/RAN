
CREATE PROCEDURE [dbo].[user_cha_remain]
	@nUserNum	int,
	@nReturn 	int	OUTPUT
AS	    
	DECLARE @nChaRemain int

	SET NOCOUNT ON

	SET @nReturn = 0
	SET @nChaRemain = 0
	
	SELECT @nChaRemain = UserInfo.ChaRemain
	FROM UserInfo
	WHERE UserNum = @nUserNum
	
	SET @nReturn = @nChaRemain

	SET NOCOUNT OFF	
	
	RETURN @nReturn


