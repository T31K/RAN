

-- daum_user_gettype
CREATE PROCEDURE [dbo].[daum_user_gettype]
	@nUserNum	int,
	@nReturn 	int	OUTPUT
AS	
	-- 0 老馆荤侩磊
	-- 1 漂喊荤侩磊 (霸烙规 诀眉 荤厘, 扁磊 殿殿)
	-- 2 GM 3 鞭
	-- 3 GM 2 鞭
	-- 4 GM 1 鞭
	-- 5 Master
    
	DECLARE @nUserType int

	SET NOCOUNT ON

	SET @nReturn = 0
	
	SELECT @nUserType = DaumUserInfo.UserType 
	FROM DaumUserInfo 
	WHERE UserNum = @nUserNum
	
	SET @nReturn = @nUserType

	SET NOCOUNT OFF	
	
	RETURN @nReturn
