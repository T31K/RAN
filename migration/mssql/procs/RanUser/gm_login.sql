
CREATE      PROCEDURE [dbo].[gm_login]
	@userId		varChar(50),
	@userPass 	varChar(50),
	@userIP		varChar(50),
	@nReturn 	int	OUTPUT
AS
	DECLARE @nUserNum	int,
		@nUserType	int

	SET NOCOUNT ON

	SET @nReturn = 0
	SET @nUserNum = 0
	
	SELECT @nUserNum = UserInfo.UserNum, @nUserType=UserInfo.UserType 
	FROM UserInfo 
	WHERE UserID = @userId AND UserPass = @userPass AND UserAvailable = 1 AND UserType>=20 

	-- PRINT @nUserNum

	-- ID / PWD 眉农...
	IF @nUserNum = 0
	BEGIN
		-- ID / PWD 阂老摹 肚绰 荤侩阂啊瓷惑怕
		SET @nReturn = 0
	END
	ELSE
	BEGIN
		-- ID / PWD 老摹
		SET @nReturn = @nUserType
		-- 立加肺弊 巢扁扁
		INSERT INTO LogGmCmd (UserNum, GmCmd, UserIP) 
		VALUES (@nUserNum, 'LOGIN GMTOOL UserID:' + @userId, @userIP)
	END

	SET NOCOUNT OFF
	
	RETURN @nReturn
