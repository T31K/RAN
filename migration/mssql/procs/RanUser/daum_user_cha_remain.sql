-- daum_user_cha_remain
CREATE PROCEDURE [dbo].[daum_user_cha_remain]
	@nUserNum	int,
	@nReturn 	int	OUTPUT
AS	    
	DECLARE @nChaRemain int

	SET NOCOUNT ON

	SET @nReturn = 0
	
	SELECT @nChaRemain = DaumUserInfo.ChaRemain
	FROM DaumUserInfo 
	WHERE UserNum = @nUserNum
	
	SET @nReturn = @nChaRemain

	SET NOCOUNT OFF	
	
	RETURN @nReturn
